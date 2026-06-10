/* Regression test for the uint32_t wraparound bounds-check bypass in
 * uart_flash_erase()/read()/write() (finding F-4339).
 *
 * Drives the real ufserver binary over a pseudo-terminal and sends an ERASE
 * command with address=0x1000 and len=0xFFFFFFFF. With the buggy uint32_t
 * guard, address+len wraps to 0xFFF (< FIRMWARE_PARTITION_SIZE+SWAP_SIZE), the
 * guard is bypassed and the erase loop walks far past the 0x21000-byte mmap,
 * crashing the server with SIGSEGV. With the 64-bit guard the command is
 * rejected and the server stays alive.
 *
 * Usage: test_overflow ./ufserver
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <pty.h>
#include <sys/wait.h>

#define CMD_HDR_WOLF  'W'
#define CMD_HDR_ERASE 0x03

int main(int argc, char *argv[])
{
    int master, slave;
    char slavename[256];
    pid_t pid;
    char tmpl[] = "/tmp/ufserver_fw_XXXXXX";
    int fd;
    uint8_t buf[64];
    int i;

    if (argc != 2) {
        fprintf(stderr, "usage: %s ./ufserver\n", argv[0]);
        return 2;
    }

    /* Create a small (non-WOLF) firmware file; mmap_firmware() pads it to
     * FIRMWARE_PARTITION_SIZE + SWAP_SIZE (0x21000). */
    fd = mkstemp(tmpl);
    if (fd < 0) { perror("mkstemp"); return 2; }
    memset(buf, 0xA5, sizeof(buf));
    if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) { perror("write"); return 2; }
    close(fd);

    if (openpty(&master, &slave, slavename, NULL, NULL) != 0) {
        perror("openpty");
        return 2;
    }

    signal(SIGPIPE, SIG_IGN);

    pid = fork();
    if (pid < 0) { perror("fork"); return 2; }
    if (pid == 0) {
        /* Child: run the server attached to the slave pty. */
        close(master);
        close(slave);
        fd = open("/dev/null", O_WRONLY);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); }
        execl(argv[1], argv[1], tmpl, slavename, (char *)NULL);
        perror("execl");
        _exit(127);
    }

    /* Parent: drive the server. */
    close(slave);
    usleep(200000); /* let the child open and configure the port */

    /* STX selecting a flash command, then the ERASE opcode. */
    buf[0] = CMD_HDR_WOLF;
    buf[1] = CMD_HDR_ERASE;
    /* address = 0x00001000 (little-endian raw bytes) */
    buf[2] = 0x00; buf[3] = 0x10; buf[4] = 0x00; buf[5] = 0x00;
    /* len = 0xFFFFFFFF -> address+len wraps to 0xFFF in uint32_t */
    buf[6] = 0xFF; buf[7] = 0xFF; buf[8] = 0xFF; buf[9] = 0xFF;
    if (write(master, buf, 10) != 10) { perror("write master"); }

    /* Drain ack bytes the server sends back so it never blocks. */
    fcntl(master, F_SETFL, O_NONBLOCK);

    /* Give the server time to process (and crash, if vulnerable). */
    for (i = 0; i < 20; i++) {
        uint8_t drain[64];
        (void)read(master, drain, sizeof(drain));
        usleep(50000);
        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            unlink(tmpl);
            if (WIFSIGNALED(status)) {
                fprintf(stderr, "FAIL: server died from signal %d "
                        "(OOB access from wrapped bounds check)\n",
                        WTERMSIG(status));
                return 1;
            }
            fprintf(stderr, "FAIL: server exited unexpectedly (status %d)\n",
                    status);
            return 1;
        }
    }

    /* Still alive after processing the malicious command: guard held. */
    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
    unlink(tmpl);
    printf("PASS: server rejected wrapped address+len and stayed alive\n");
    return 0;
}
