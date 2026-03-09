#include "api.h"
#include "backlight.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>

static int server_fd = -1;
static pthread_t api_thread;
static volatile int api_running = 0;
static int current_brightness = 100;
static volatile int current_state = 1;

static void send_response(int fd, int code, const char *body) {
    char buf[512];
    size_t body_len = strlen(body);
    int len = snprintf(buf, sizeof(buf),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n%s",
        code, code == 200 ? "OK" : "Bad Request",
        body_len, body);
    if (write(fd, buf, len) < 0)
        fprintf(stderr, "Warning: API write failed\n");
}

static void handle_get(int fd) {
    char body[128];
    snprintf(body, sizeof(body),
        "{\"state\":\"%s\",\"brightness\":%d}",
        current_state ? "on" : "off", current_brightness);
    send_response(fd, 200, body);
}

static void handle_post(int fd, const char *body) {
    const char *s = strstr(body, "\"state\"");
    if (s) {
        if (strstr(s, "\"on\"")) {
            current_state = 1;
            backlight_set(current_brightness);
        } else if (strstr(s, "\"off\"")) {
            current_state = 0;
            backlight_off();
        }
    }

    const char *b = strstr(body, "\"brightness\"");
    if (b) {
        b += strlen("\"brightness\"");
        while (*b == ':' || *b == ' ') b++;
        int val = (int)strtol(b, NULL, 10);
        if (val < 0) val = 0;
        if (val > 100) val = 100;
        current_brightness = val;
        if (val > 0) {
            current_state = 1;
            backlight_set(val);
        } else {
            current_state = 0;
            backlight_off();
        }
    }

    handle_get(fd);
}

static void handle_request(int fd) {
    char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';

    if (!strstr(buf, "/backlight")) {
        send_response(fd, 404, "{\"error\":\"not found\"}");
        return;
    }

    if (strncmp(buf, "GET ", 4) == 0) {
        handle_get(fd);
    } else if (strncmp(buf, "POST ", 5) == 0) {
        char *body = strstr(buf, "\r\n\r\n");
        handle_post(fd, body ? body + 4 : "");
    } else {
        send_response(fd, 400, "{\"error\":\"bad request\"}");
    }
}

static void *api_loop(void *arg) {
    (void)arg;
    while (api_running) {
        struct sockaddr_in client;
        socklen_t len = sizeof(client);
        int fd = accept(server_fd, (struct sockaddr *)&client, &len);
        if (fd < 0) {
            if (api_running) {
                struct timespec ts = { .tv_nsec = 10000000 };
                nanosleep(&ts, NULL);
            }
            continue;
        }

        struct timeval tv = { .tv_sec = 1 };
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        handle_request(fd);
        close(fd);
    }
    return NULL;
}

void api_set_state(int on) {
    current_state = on;
}

int api_get_state(void) {
    return current_state;
}

int api_get_brightness(void) {
    return current_brightness;
}

void api_set_brightness(int val) {
    if (val < 1) val = 1;
    if (val > 100) val = 100;
    current_brightness = val;
}

int api_start(int port) {
    if (port <= 0) return -1;

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) return -1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = INADDR_ANY
    };

    if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        fprintf(stderr, "Warning: API bind failed on port %d: %s\n", port, strerror(errno));
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    if (listen(server_fd, 4) < 0) {
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    api_running = 1;
    if (pthread_create(&api_thread, NULL, api_loop, NULL) != 0) {
        api_running = 0;
        close(server_fd);
        server_fd = -1;
        return -1;
    }

    fprintf(stderr, "API listening on port %d\n", port);
    return 0;
}

void api_stop(void) {
    if (!api_running) return;
    api_running = 0;
    if (server_fd >= 0) {
        shutdown(server_fd, SHUT_RDWR);
        close(server_fd);
        server_fd = -1;
    }
    pthread_join(api_thread, NULL);
}
