# SPDX-License-Identifier: GPL-3.0-only
"""Bounded process-level acceptance checks for the authority-session prototype.

Run with: python3 process_tests.py /path/to/authority-session

These checks exercise the local CLI and loopback UDP protocol only. They do not
claim native game integration or production network behavior.
"""

import json
import os
import queue
import re
import signal
import socket
import subprocess
import sys
import threading
import time
import unittest


if len(sys.argv) < 2:
    raise SystemExit("usage: process_tests.py /path/to/authority-session [unittest options]")
EXE = os.path.abspath(sys.argv.pop(1))
IO_TIMEOUT = 3.0
STOP_TIMEOUT = 1.0
EOF = object()


class JsonProcess:
    """A child process whose stdout is a newline-delimited JSON event stream."""

    def __init__(self, args):
        self.args = [EXE, *map(str, args)]
        self.events = queue.Queue()
        self.stdout_lines = []
        self.stderr_lines = []
        self.stderr_lock = threading.Lock()
        self.peer = None
        self.process = subprocess.Popen(
            self.args,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
            start_new_session=True,
        )
        self.stdout_thread = threading.Thread(target=self._read_stdout, daemon=True)
        self.stderr_thread = threading.Thread(target=self._read_stderr, daemon=True)
        self.stdout_thread.start()
        self.stderr_thread.start()

    def _read_stdout(self):
        try:
            for line in self.process.stdout:
                line = line.rstrip("\r\n")
                if line:
                    self.stdout_lines.append(line)
                    self.events.put(line)
        finally:
            self.events.put(EOF)

    def _read_stderr(self):
        for line in self.process.stderr:
            with self.stderr_lock:
                self.stderr_lines.append(line.rstrip("\r\n"))

    @property
    def stderr(self):
        with self.stderr_lock:
            return "\n".join(self.stderr_lines)

    def details(self):
        return (
            f"command={self.args!r}, exit={self.process.poll()}, "
            f"stdout={self.stdout_lines!r}, stderr={self.stderr!r}"
        )

    def next_event(self, kind=None, timeout=IO_TIMEOUT, predicate=None):
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise AssertionError(f"timed out waiting for {kind or 'JSON event'}: {self.details()}")
            try:
                line = self.events.get(timeout=remaining)
            except queue.Empty as exc:
                raise AssertionError(
                    f"timed out waiting for {kind or 'JSON event'}: {self.details()}"
                ) from exc
            if line is EOF:
                raise AssertionError(f"process closed stdout before {kind or 'JSON event'}: {self.details()}")
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                raise AssertionError(f"stdout was not JSON ({line!r}): {self.details()}") from exc
            if event.get("kind") == "context":
                self.peer = event.get("peer")
            if kind is not None and event.get("kind") != kind:
                continue
            # A listen host's stdout can also carry remote command receipts.
            # Match responses to the process whose stdin received the command.
            if kind in ("receipt", "snapshot") and self.peer is not None:
                if event.get("peer") != self.peer:
                    continue
            if predicate is not None and not predicate(event):
                continue
            return event

    def assert_no_event(self, kind, timeout=0.25):
        deadline = time.monotonic() + timeout
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return
            try:
                line = self.events.get(timeout=remaining)
            except queue.Empty:
                return
            if line is EOF:
                raise AssertionError(f"process closed stdout while waiting for no {kind}: {self.details()}")
            try:
                event = json.loads(line)
            except json.JSONDecodeError as exc:
                raise AssertionError(f"stdout was not JSON ({line!r}): {self.details()}") from exc
            if event.get("kind") == kind:
                raise AssertionError(f"unexpected {kind} event during no-rejoin window: {event!r}")

    def send(self, line):
        if self.process.poll() is not None:
            raise AssertionError(f"cannot write to exited process: {self.details()}")
        self.process.stdin.write(line + "\n")
        self.process.stdin.flush()

    def command(self, epoch, request_id, entity, delta):
        self.send(f"command {epoch} {request_id} {entity} {delta}")
        return self.next_event("receipt")

    def snapshot(self):
        self.send("snapshot")
        return self.next_event("snapshot")

    def wait_exit(self, timeout=IO_TIMEOUT):
        try:
            return self.process.wait(timeout=timeout)
        except subprocess.TimeoutExpired as exc:
            raise AssertionError(f"process did not exit in {timeout}s: {self.details()}") from exc

    def stop(self, graceful=True):
        if self.process.poll() is None and graceful:
            try:
                self.send("quit")
                self.process.wait(timeout=STOP_TIMEOUT)
            except (AssertionError, BrokenPipeError, subprocess.TimeoutExpired):
                pass
        if self.process.poll() is None:
            self._signal_group(signal.SIGTERM)
            try:
                self.process.wait(timeout=STOP_TIMEOUT)
            except subprocess.TimeoutExpired:
                self._signal_group(signal.SIGKILL)
                self.process.wait(timeout=STOP_TIMEOUT)
        self.stdout_thread.join(timeout=0.2)
        self.stderr_thread.join(timeout=0.2)
        self._close_pipes()

    def crash(self):
        if self.process.poll() is None:
            self._signal_group(signal.SIGKILL)
            self.process.wait(timeout=STOP_TIMEOUT)
        self.stdout_thread.join(timeout=0.2)
        self.stderr_thread.join(timeout=0.2)
        self._close_pipes()

    def _close_pipes(self):
        for stream in (self.process.stdin, self.process.stdout, self.process.stderr):
            if stream is not None:
                try:
                    stream.close()
                except OSError:
                    pass

    def _signal_group(self, sig):
        try:
            os.killpg(self.process.pid, sig)
        except ProcessLookupError:
            pass


def free_loopback_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def check_context(
    test, event, expected_epoch=None, expected_peer=None, expected_status="Ok", owned=True
):
    test.assertEqual(event.get("kind"), "context", event)
    test.assertEqual(event.get("status"), expected_status, event)
    epoch = event.get("epoch")
    test.assertIsInstance(epoch, str, event)
    test.assertRegex(epoch, re.compile(r"^[0-9a-f]{32}$"), event)
    test.assertIsInstance(event.get("peer"), int, event)
    test.assertIsInstance(event.get("entity"), int, event)
    if owned:
        test.assertGreater(event["entity"], 0, event)
    else:
        test.assertEqual(event["entity"], 0, event)
    if expected_epoch is not None:
        test.assertEqual(epoch, expected_epoch, event)
    if expected_peer is not None:
        test.assertEqual(event["peer"], expected_peer, event)
    return event


class LocalSession:
    """One loopback session. Dedicated reserves peer 1; listen owns peer 1 locally."""

    def __init__(self, mode):
        self.mode = mode
        self.port = free_loopback_port()
        self.server = None
        self.clients = []
        try:
            self.start_server()
        except BaseException:
            if self.server is not None:
                self.server.stop()
            raise

    def start_server(self):
        self.server = JsonProcess([self.mode, self.port])
        if self.mode == "dedicated":
            ready = self.server.next_event("ready")
            if "peer" in ready:
                if ready["peer"] != 0:
                    raise AssertionError(f"dedicated ready event should be peer 0: {ready!r}")
            self.host_context = None
        else:
            self.host_context = check_context(
                unittest.TestCase(), self.server.next_event("context"), expected_peer=1
            )

    def client(self, expected_status="Ok"):
        child = JsonProcess(["client", self.port])
        self.clients.append(child)
        context = check_context(
            unittest.TestCase(),
            child.next_event("context"),
            expected_status=expected_status,
            owned=expected_status == "Ok",
        )
        if self.host_context is not None:
            if context["epoch"] != self.host_context["epoch"]:
                raise AssertionError(f"client joined a different epoch: {context!r}")
        return child, context

    def kill_server(self):
        self.server.crash()

    def close(self):
        for child in reversed(self.clients):
            child.stop()
        if self.server is not None:
            self.server.stop()


class AuthorityProcessAcceptance(unittest.TestCase):
    def setUp(self):
        self.sessions = []
        self.processes = []

    def tearDown(self):
        for session in reversed(self.sessions):
            session.close()
        for process in reversed(self.processes):
            process.stop()

    def session(self, mode):
        result = LocalSession(mode)
        self.sessions.append(result)
        return result

    def process(self, args):
        result = JsonProcess(args)
        self.processes.append(result)
        return result

    @staticmethod
    def assert_receipt(test, result, status, request_id=None):
        test.assertEqual(result.get("kind"), "receipt", result)
        test.assertEqual(result.get("status"), status, result)
        if request_id is not None:
            test.assertEqual(result.get("id"), request_id, result)
        return result

    def test_dedicated_two_client_processes_enforce_entity_ownership(self):
        session = self.session("dedicated")
        client_a, context_a = session.client()
        client_b, context_b = session.client()
        self.assertEqual(context_a["epoch"], context_b["epoch"])
        self.assertGreaterEqual(context_a["peer"], 2)  # peer 1 is reserved for the host
        self.assertNotEqual(context_a["peer"], context_b["peer"])
        self.assertNotEqual(context_a["entity"], context_b["entity"])

        self.assert_receipt(
            self, client_a.command(context_a["epoch"], 1, context_a["entity"], 11), "Applied", 1
        )
        self.assert_receipt(
            self, client_b.command(context_b["epoch"], 1, context_b["entity"], 22), "Applied", 1
        )
        self.assert_receipt(
            self, client_a.command(context_a["epoch"], 2, context_b["entity"], 7), "Forbidden", 2
        )
        self.assert_receipt(
            self, client_b.command(context_b["epoch"], 2, context_a["entity"], 7), "Forbidden", 2
        )
        self.assertEqual(client_a.snapshot()["value"], 11)
        self.assertEqual(client_b.snapshot()["value"], 22)

    def test_listen_host_local_and_remote_use_the_same_ownership_policy(self):
        session = self.session("listen")
        host = session.server
        host_context = session.host_context
        remote, remote_context = session.client()
        self.assertNotEqual(host_context["entity"], remote_context["entity"])

        self.assert_receipt(
            self, host.command(host_context["epoch"], 1, host_context["entity"], 3), "Applied", 1
        )
        self.assert_receipt(
            self,
            remote.command(remote_context["epoch"], 1, remote_context["entity"], 5),
            "Applied",
            1,
        )
        self.assert_receipt(
            self,
            host.command(host_context["epoch"], 2, remote_context["entity"], 1),
            "Forbidden",
            2,
        )
        self.assert_receipt(
            self,
            remote.command(remote_context["epoch"], 2, host_context["entity"], 1),
            "Forbidden",
            2,
        )
        self.assertEqual(host.snapshot()["value"], 3)
        self.assertEqual(remote.snapshot()["value"], 5)

    def test_dedicated_server_rejects_local_command_and_snapshot(self):
        session = self.session("dedicated")
        client, context = session.client()
        session.server.send(f"command {context['epoch']} 1 {context['entity']} 7")
        command_error = session.server.next_event("error")
        self.assertEqual(command_error.get("status"), "NoLocalPeer", command_error)
        session.server.send("snapshot")
        snapshot_error = session.server.next_event("error")
        self.assertEqual(snapshot_error.get("status"), "NoLocalPeer", snapshot_error)
        self.assertEqual(client.snapshot()["value"], 0)

    def test_duplicate_conflict_and_snapshot_are_stable(self):
        session = self.session("dedicated")
        client, context = session.client()
        epoch, entity = context["epoch"], context["entity"]
        first = self.assert_receipt(self, client.command(epoch, 1, entity, 6), "Applied", 1)
        after_first = client.snapshot()
        duplicate = self.assert_receipt(self, client.command(epoch, 1, entity, 6), "Applied", 1)
        self.assertEqual(duplicate, first)
        self.assertEqual(client.snapshot(), after_first)

        conflict = self.assert_receipt(self, client.command(epoch, 1, entity, -6), "Conflict", 1)
        self.assertEqual(conflict["value"], 6)
        self.assertEqual(conflict["revision"], after_first["revision"])
        self.assertEqual(client.snapshot(), after_first)

    def test_gap_rejects_then_accepts_the_missing_sequence(self):
        session = self.session("dedicated")
        client, context = session.client()
        epoch, entity = context["epoch"], context["entity"]
        self.assert_receipt(self, client.command(epoch, 2, entity, 4), "Gap", 2)
        self.assertEqual(client.snapshot()["value"], 0)
        self.assert_receipt(self, client.command(epoch, 1, entity, 4), "Applied", 1)
        self.assert_receipt(self, client.command(epoch, 2, entity, 5), "Applied", 2)
        self.assertEqual(client.snapshot()["value"], 9)

    def test_out_of_bounds_values_do_not_mutate_state(self):
        session = self.session("dedicated")
        client, context = session.client()
        epoch, entity = context["epoch"], context["entity"]
        self.assert_receipt(self, client.command(epoch, 1, entity, 101), "Invalid", 1)
        self.assert_receipt(self, client.command(epoch, 2, entity, -101), "Invalid", 2)
        self.assertEqual(client.snapshot()["value"], 0)
        self.assert_receipt(self, client.command(epoch, 3, entity, 100), "Applied", 3)
        self.assertEqual(client.snapshot()["value"], 100)

    def test_evicted_receipt_is_old_and_never_reapplies(self):
        session = self.session("dedicated")
        client, context = session.client()
        epoch, entity = context["epoch"], context["entity"]
        self.assert_receipt(self, client.command(epoch, 1, entity, 9), "Applied", 1)
        for request_id in range(2, 10):
            self.assert_receipt(
                self, client.command(epoch, request_id, entity, 1), "Applied", request_id
            )
        before_retry = client.snapshot()
        self.assert_receipt(self, client.command(epoch, 1, entity, 9), "Old", 1)
        self.assertEqual(client.snapshot(), before_retry)
        self.assertEqual(before_retry["value"], 17)

    def test_disconnect_and_explicit_reconnect_assign_fresh_ownership(self):
        session = self.session("dedicated")
        old_client, old_context = session.client()
        self.assert_receipt(
            self,
            old_client.command(old_context["epoch"], 1, old_context["entity"], 5),
            "Applied",
            1,
        )
        old_client.send("disconnect")
        old_client.wait_exit()

        new_client, new_context = session.client()
        self.assertEqual(new_context["epoch"], old_context["epoch"])
        self.assertNotEqual(new_context["peer"], old_context["peer"])
        self.assertNotEqual(new_context["entity"], old_context["entity"])
        self.assert_receipt(
            self,
            new_client.command(
                new_context["epoch"], 1, old_context["entity"], 99
            ),
            "Forbidden",
            1,
        )
        self.assertEqual(new_client.snapshot()["value"], 0)
        self.assert_receipt(
            self,
            new_client.command(new_context["epoch"], 2, new_context["entity"], 2),
            "Applied",
            2,
        )
        self.assertEqual(new_client.snapshot()["value"], 2)

    def test_restart_changes_epoch_clears_values_and_rejects_stale_epoch(self):
        session = self.session("dedicated")
        old_client, old_context = session.client()
        self.assert_receipt(
            self,
            old_client.command(old_context["epoch"], 1, old_context["entity"], 8),
            "Applied",
            1,
        )
        self.assertEqual(old_client.snapshot()["value"], 8)
        old_client.send("quit")
        old_client.wait_exit()

        session.kill_server()
        session.start_server()
        fresh_client, fresh_context = session.client()
        self.assertNotEqual(fresh_context["epoch"], old_context["epoch"])
        self.assertEqual(fresh_client.snapshot()["value"], 0)
        stale = self.assert_receipt(
            self,
            fresh_client.command(old_context["epoch"], 1, fresh_context["entity"], 4),
            "Stale",
            1,
        )
        self.assertEqual(stale["value"], 0)
        self.assertEqual(fresh_client.snapshot()["value"], 0)
        self.assert_receipt(
            self,
            fresh_client.command(fresh_context["epoch"], 1, fresh_context["entity"], 4),
            "Applied",
            1,
        )
        self.assertEqual(fresh_client.snapshot()["value"], 4)

    def test_solo_uses_local_commands_and_opens_no_socket(self):
        solo = self.process(["solo"])
        context = check_context(self, solo.next_event("context"), expected_peer=1)
        self.assert_no_socket_fds(solo.process.pid)
        self.assert_receipt(
            self,
            solo.command(context["epoch"], 1, context["entity"], 12),
            "Applied",
            1,
        )
        self.assertEqual(solo.snapshot()["value"], 12)
        self.assert_no_socket_fds(solo.process.pid)

    def test_input_length_and_integer_overflow_are_rejected_without_mutation(self):
        solo = self.process(["solo"])
        context = check_context(self, solo.next_event("context"), expected_peer=1)
        epoch, entity = context["epoch"], context["entity"]

        exact_limit = f"command {epoch} 1 {entity} 1"
        self.assertLessEqual(len(exact_limit), 256)
        exact_limit = exact_limit + (" " * (256 - len(exact_limit)))
        self.assertEqual(len(exact_limit.encode("ascii")), 256)
        solo.send(exact_limit)
        self.assert_receipt(self, solo.next_event("receipt"), "Applied", 1)
        before = solo.snapshot()
        self.assertEqual(before["value"], 1)

        too_long = f"command {epoch} 2 {entity} 99"
        too_long = too_long + (" " * (257 - len(too_long)))
        self.assertEqual(len(too_long.encode("ascii")), 257)
        solo.send(too_long)
        length_error = solo.next_event("error")
        self.assertEqual(length_error.get("status"), "InvalidInput", length_error)
        self.assertEqual(solo.snapshot(), before)

        solo.send(f"command {epoch} 18446744073709551616 {entity} 1")
        id_overflow = solo.next_event("error")
        self.assertEqual(id_overflow.get("status"), "InvalidInput", id_overflow)
        solo.send(f"command {epoch} 2 {entity} 9223372036854775808")
        delta_overflow = solo.next_event("error")
        self.assertEqual(delta_overflow.get("status"), "InvalidInput", delta_overflow)
        self.assertEqual(solo.snapshot(), before)

        self.assert_receipt(self, solo.command(epoch, 2, entity, 2), "Applied", 2)
        self.assertEqual(solo.snapshot()["value"], 3)

    def test_output_backpressure_exits_child_with_bounded_failure(self):
        child = subprocess.Popen(
            [EXE, "solo"],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            start_new_session=True,
        )
        payload = b"snapshot\n" * 50000
        writer_errors = []

        def write_snapshots():
            try:
                view = memoryview(payload)
                offset = 0
                while offset < len(view):
                    offset += os.write(child.stdin.fileno(), view[offset : offset + 4096])
            except (BrokenPipeError, OSError) as exc:
                writer_errors.append(exc)

        writer = threading.Thread(target=write_snapshots, daemon=True)
        writer.start()
        try:
            returncode = child.wait(timeout=4.0)
        except subprocess.TimeoutExpired:
            try:
                os.killpg(child.pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            child.wait(timeout=STOP_TIMEOUT)
            self.fail("child did not exit when stdout backpressure filled the pipe")
        finally:
            writer.join(timeout=0.5)
            for stream in (child.stdin, child.stdout, child.stderr):
                if stream is not None:
                    try:
                        stream.close()
                    except OSError:
                        pass
        self.assertEqual(returncode, 2, f"writer_errors={writer_errors!r}")

    def test_peer_capacity_is_lifetime_bounded_after_sequential_leaves(self):
        session = self.session("dedicated")
        seen_peers = set()
        seen_entities = set()
        epoch = None
        for _ in range(16):
            client, context = session.client()
            if epoch is None:
                epoch = context["epoch"]
            self.assertEqual(context["epoch"], epoch)
            self.assertEqual(context["status"], "Ok")
            self.assertGreater(context["entity"], 0)
            self.assertNotIn(context["peer"], seen_peers)
            self.assertNotIn(context["entity"], seen_entities)
            seen_peers.add(context["peer"])
            seen_entities.add(context["entity"])
            client.send("disconnect")
            client.wait_exit()
            left = session.server.next_event(
                "peer",
                predicate=lambda event, expected_peer=context["peer"]: (
                    event.get("peer") == expected_peer and event.get("status") == "Left"
                ),
            )
            self.assertEqual(left["status"], "Left")

        self.assertEqual(len(seen_peers), 16)
        self.assertEqual(len(seen_entities), 16)
        overloaded, context = session.client(expected_status="Overload")
        self.assertEqual(context.get("kind"), "context", context)
        self.assertEqual(context.get("status"), "Overload", context)
        self.assertEqual(context.get("epoch"), epoch)
        self.assertEqual(context.get("entity"), 0)
        self.assertNotIn(context.get("peer"), seen_peers)
        server_refusal = session.server.next_event(
            "peer",
            predicate=lambda event, expected_peer=context["peer"]: (
                event.get("peer") == expected_peer and event.get("status") == "Overload"
            ),
        )
        self.assertEqual(server_refusal["status"], "Overload")
        self.assertEqual(overloaded.snapshot().get("status"), "Disconnected")
        self.assertEqual(overloaded.snapshot().get("status"), "Disconnected")
        overloaded.assert_no_event("context", timeout=0.25)
        self.assertIsNone(overloaded.process.poll(), "overloaded peer should not auto-reconnect or exit")

    def test_graceful_host_shutdown_reports_down_without_auto_rejoin(self):
        session = self.session("dedicated")
        client, context = session.client()
        self.assertEqual(context["status"], "Ok")
        session.server.send("quit")
        self.assertEqual(session.server.wait_exit(), 0)
        down = client.next_event("state")
        self.assertEqual(down.get("status"), "Down", down)
        client.assert_no_event("context", timeout=0.25)
        self.assertIsNone(client.process.poll(), "client should remain idle without automatically rejoining")

    def assert_no_socket_fds(self, pid):
        fd_dir = f"/proc/{pid}/fd"
        if not os.path.isdir(fd_dir):
            self.skipTest("socket descriptor inspection requires /proc")
        socket_fds = []
        for name in os.listdir(fd_dir):
            try:
                target = os.readlink(os.path.join(fd_dir, name))
            except FileNotFoundError:
                continue
            if target.startswith("socket:"):
                socket_fds.append((name, target))
        self.assertEqual(socket_fds, [], f"solo process opened sockets: {socket_fds!r}")

    def test_sc_world_mode_fails_closed(self):
        try:
            result = subprocess.run(
                [EXE, "sc-world"],
                input="",
                capture_output=True,
                text=True,
                timeout=IO_TIMEOUT,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            self.fail(f"sc-world did not fail promptly: {exc}")
        self.assertNotEqual(result.returncode, 0, result.stdout)
        combined = result.stdout + result.stderr
        self.assertTrue(combined.strip(), "sc-world must explain its fail-closed result")
        self.assertNotIn('"status":"Applied"', combined)


if __name__ == "__main__":
    unittest.main(verbosity=2)
