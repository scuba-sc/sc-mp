# SPDX-License-Identifier: GPL-3.0-only
"""Run the shared process cases with a bounded SDK-service startup allowance.

Instrumented host/client startup can exceed the shared harness deadline with
the SDK's normal session settings. Keep the shared scenarios and their explicit
short negative-event checks intact.
"""
import os
import sys
import unittest

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "authority-session"))
import process_tests as harness

SERVICE_IO_TIMEOUT = 10.0


class ServiceJsonProcess(harness.JsonProcess):
    def next_event(self, kind=None, timeout=SERVICE_IO_TIMEOUT, predicate=None):
        return super().next_event(kind, timeout=timeout, predicate=predicate)

    def wait_exit(self, timeout=SERVICE_IO_TIMEOUT):
        return super().wait_exit(timeout=timeout)


harness.JsonProcess = ServiceJsonProcess

if __name__ == "__main__":
    unittest.main(module=harness, verbosity=2)
