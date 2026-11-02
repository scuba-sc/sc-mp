# SPDX-License-Identifier: GPL-3.0-only
"""Discard one application receipt, then retry over the same live SDK peer."""
import sys
import unittest

sys.dont_write_bytecode = True
from service_process_tests import harness


class ReceiptLoss(unittest.TestCase):
    def test_same_peer_retry_returns_original_outcome(self):
        session = harness.LocalSession("dedicated")
        try:
            client, context = session.client()
            request = f"command {context['epoch']} 1 {context['entity']} 9"
            client.send(request)
            dropped = client.next_event("receipt")  # application deliberately discards it
            self.assertEqual(dropped["status"], "Applied")
            self.assertEqual(client.snapshot()["value"], 9)
            client.send(request)
            retry = client.next_event("receipt")
            self.assertEqual(retry, dropped)
            self.assertEqual(client.snapshot()["value"], 9)
        finally:
            session.close()


if __name__ == "__main__":
    unittest.main(verbosity=2)
