"""Exercise the real first-boot setup/login path for ordinary guest suites."""
TEST_PASSWORD = "HarnessPass123"


def setup_and_login(qmp, output, wait_for):
    def wait_text(text, offset=0):
        wait_for(lambda: text in output()[offset:], repr(text))
    wait_text("First-boot setup.")
    qmp.type("root\n")
    wait_text("New password: ")
    qmp.type(TEST_PASSWORD + "\n")
    wait_text("Confirm password: ")
    qmp.type(TEST_PASSWORD + "\n")
    wait_text("Setup complete. Please log in.")
    offset = len(output())
    qmp.type("root\n")
    wait_text("Password: ", offset)
    qmp.type(TEST_PASSWORD + "\n")
    wait_text("root@root:~$ ", offset)
    assert TEST_PASSWORD not in output(), "Password leaked to console/debug output"
