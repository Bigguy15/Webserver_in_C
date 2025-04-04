#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#define SV_IMPLEMENTATION
#include "string_view.h"

#define BUFFER_SIZE 4096
#define PORT 4221
#define FILES_DIR "./files/"

// ================== Function Prototypes ==================
// Response handlers
void send_response(int client_fd, const char *status, const char *content_type, const char *body);
void send_200(int client_fd);
void send_201(int client_fd);
void send_400(int client_fd);
void send_403(int client_fd);
void send_404(int client_fd);
void send_405(int client_fd);
void send_500(int client_fd);

// Request handlers
void handle_root(int client_fd);
void handle_echo(int client_fd, string_view *path);
void handle_user_agent(int client_fd, string_view *user_agent);
void handle_file_request(int client_fd, string_view *method, string_view *path, string_view *body, size_t content_length);
void handle_image_page(int client_fd);

// Utility functions
void error(const char *msg);
const char *get_mime_type(const char *filename);
void serve_file(int client_fd, const char *file_path);
void post_file(int client_fd, const char *file_path, string_view *body, size_t content_length);

// Parsing functions
void parse_request_line(string_view *request, string_view *method, string_view *path, string_view *protocol);
string_view parse_headers(string_view *request, size_t *content_length);
string_view parse_http_request(const char *raw_request, size_t raw_request_len,
                              string_view *method, string_view *path, string_view *protocol,
                              string_view *user_agent, size_t *content_length);

// ================== Main Server Functions ==================
void *handle_client(void *arg) {
    int client_fd = *(int *)arg;
    free(arg);
    char buffer[BUFFER_SIZE];

    ssize_t bytes_received = recv(client_fd, buffer, BUFFER_SIZE - 1, 0);
    if (bytes_received <= 0) {
        close(client_fd);
        return NULL;
    }

    buffer[bytes_received] = '\0';
    printf("Received request:\n%s\n", buffer);

    string_view method, path, protocol, user_agent;
    size_t content_length = 0;
    string_view body = parse_http_request(buffer, bytes_received, &method, &path, 
                                        &protocol, &user_agent, &content_length);

    // Route requests
    if (sv_equals(path, SV_LITERAL("/"), true)) {
        handle_root(client_fd);
    } 
    else if (sv_has_prefix(path, SV_LITERAL("/echo/"), true)) {
        handle_echo(client_fd, &path);
    } 
    else if (sv_equals(path, SV_LITERAL("/user-agent"), true)) {
        handle_user_agent(client_fd, &user_agent);
    } 
    else if (sv_has_prefix(path, SV_LITERAL("/files/"), true)) {
        handle_file_request(client_fd, &method, &path, &body, content_length);
    }
    else if (sv_equals(path, SV_LITERAL("/image"), true)) {
        handle_image_page(client_fd);
    }
    else {
        send_404(client_fd);
    }

    close(client_fd);
    return NULL;
}

int main() {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // Server setup
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) error("Socket creation failed");

    int reuse = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in serv_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(PORT),
        .sin_addr = { htonl(INADDR_ANY) }
    };

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) != 0) {
        error("Bind failed");
    }

    listen(server_fd, 5);
    printf("Waiting for connections on port %d...\n", PORT);

    // Main server loop
    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd < 0) error("Accept failed");

        int *client_fd_ptr = malloc(sizeof(int));
        *client_fd_ptr = client_fd;

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, client_fd_ptr) != 0) {
            perror("pthread_create");
            free(client_fd_ptr);
            close(client_fd);
            continue;
        }

        pthread_detach(tid);
    }

    close(server_fd);
    return 0;
}

// ================== Request Handlers ==================
void handle_root(int client_fd) {
    send_200(client_fd);
}

void handle_echo(int client_fd, string_view *path) {
    string_view echo_prefix = SV_LITERAL("/echo/");
    sv_chop_prefix(path, echo_prefix);
    
    char response[4096];
    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: %zu\r\n\r\n"
             SV_FMT,
             path->size, SV_ARG_POINTER(path));
    send(client_fd, response, strlen(response), 0);
}

void handle_user_agent(int client_fd, string_view *user_agent) {
    char response[4096];
    snprintf(response, sizeof(response),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/plain\r\n"
             "Content-Length: %zu\r\n\r\n"
             SV_FMT,
             user_agent->size, SV_ARG(*user_agent));
    send(client_fd, response, strlen(response), 0);
}

void handle_file_request(int client_fd, string_view *method, string_view *path, 
                        string_view *body, size_t content_length) {
    string_view files_prefix = SV_LITERAL("/files/");
    sv_chop_prefix(path, files_prefix);
    
    // Normalize path
    while (path->size > 0 && path->data[0] == '/') {
        sv_chop(path, 1);
    }

    // Validate filename
    char filename[4096];
    if (path->size >= sizeof(filename)) {
        send_400(client_fd);
        close(client_fd);
        return;
    }
    memcpy(filename, path->data, path->size);
    filename[path->size] = '\0';

    if (strstr(filename, "..") != NULL) {
        send_403(client_fd);
        close(client_fd);
        return;
    }

    char file_path[4096];
    snprintf(file_path, sizeof(file_path), "%s%s", FILES_DIR, filename);

    if (sv_equals(*method, SV_LITERAL("GET"), true)) {
        serve_file(client_fd, file_path);
    } 
    else if (sv_equals(*method, SV_LITERAL("POST"), true)) {
        post_file(client_fd, file_path, body, content_length);
    } 
    else {
        send_405(client_fd);
    }
}

void handle_image_page(int client_fd) {
    const char *html_template =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %zu\r\n"
        "\r\n"
        "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>Image Viewer</title>\n"
        "</head>\n"
        "<body>\n"
        "    <h1>Image</h1>\n"
        "    <img src=\"/files/image.jpg\" alt=\"Example Image\">\n"
        "</body>\n"
        "</html>";

    size_t html_length = strlen(html_template) - 2; // Exclude %zu placeholder
    char response[4096];
    snprintf(response, sizeof(response), html_template, html_length);
    send(client_fd, response, strlen(response), 0);
}

// ================== Utility Functions ==================
void error(const char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

const char *get_mime_type(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return "application/octet-stream";

    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    
    return "application/octet-stream";
}

void serve_file(int client_fd, const char *file_path) {
    FILE *fp = fopen(file_path, "rb");
    if (!fp) {
        send_404(client_fd);
        return;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const char *mime_type = get_mime_type(file_path);

    char response_header[4096];
    snprintf(response_header, sizeof(response_header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %ld\r\n"
             "\r\n",
             mime_type, file_size);

    send(client_fd, response_header, strlen(response_header), 0);

    char buffer[BUFFER_SIZE];
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, BUFFER_SIZE, fp)) > 0) {
        send(client_fd, buffer, bytes_read, 0);
    }

    fclose(fp);
}

void post_file(int client_fd, const char *file_path, string_view *body, size_t content_length) {
    FILE *fp = fopen(file_path, "wb");
    if (!fp) {
        printf("ERROR: Failed to write file\n");
        send_500(client_fd);
        close(client_fd);
        return;
    }

    printf("Writing to file: %s\n", file_path);
    size_t bytes_written = 0;

    // Write initial body data if present
    if (body->size > 0) {
        fwrite(body->data, 1, body->size, fp);
        bytes_written += body->size;
    }

    // Read remaining data if needed
    while (bytes_written < content_length) {
        char body_buffer[BUFFER_SIZE];
        ssize_t bytes_read = recv(client_fd, body_buffer, BUFFER_SIZE, 0);
        
        if (bytes_read <= 0) {
            fclose(fp);
            send_400(client_fd);
            close(client_fd);
            return;
        }

        fwrite(body_buffer, 1, bytes_read, fp);
        bytes_written += bytes_read;
    }

    fclose(fp);
    printf("Wrote %zu bytes to file: %s\n", bytes_written, file_path);
    send_201(client_fd);
}

// ================== Response Functions ==================
void send_response(int client_fd, const char *status, const char *content_type, const char *body) {
    char response[4096];
    snprintf(response, sizeof(response),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n\r\n"
             "%s",
             status, content_type, strlen(body), body);
    send(client_fd, response, strlen(response), 0);
}

void send_200(int client_fd) {
    const char *response = 
        "HTTP/1.1 200 OK\r\n\r\n"
        "<html><body><h1>Welcome To My Homepage</h1></body></html>";
    send(client_fd, response, strlen(response), 0);
}

void send_201(int client_fd) {
    const char *response = "HTTP/1.1 201 Created\r\n\r\n";
    send(client_fd, response, strlen(response), 0);
}

void send_400(int client_fd) {
    const char *response = "HTTP/1.1 400 Bad Request\r\n\r\n";
    send(client_fd, response, strlen(response), 0);
}

void send_403(int client_fd) {
    const char *response = "HTTP/1.1 403 Forbidden\r\n\r\n";
    send(client_fd, response, strlen(response), 0);
}

void send_404(int client_fd) {
    const char *response = 
        "HTTP/1.1 404 Not Found\r\n\r\n"
        "<html><body><h1>NOT FOUND 404</h1></body></html>";
    send(client_fd, response, strlen(response), 0);
}

void send_405(int client_fd) {
    const char *response = "HTTP/1.1 405 Method Not Allowed\r\n\r\n";
    send(client_fd, response, strlen(response), 0);
}

void send_500(int client_fd) {
    const char *response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
    send(client_fd, response, strlen(response), 0);
}

// ================== Parsing Functions ==================
void parse_request_line(string_view *request, string_view *method, string_view *path, string_view *protocol) {
    string_view request_line = sv_chop_char(request, '\n');
    if (request_line.size == 0) {
        fprintf(stderr, "Invalid HTTP request: missing request line\n");
        exit(EXIT_FAILURE);
    }
    *method = sv_chop_char(&request_line, ' ');
    *path = sv_chop_char(&request_line, ' ');
    *protocol = sv_chop_char(&request_line, '\r');
}

string_view parse_headers(string_view *request, size_t *content_length) {
    *content_length = 0;
    string_view user_agent = SV_LITERAL("");

    while (request->size > 0) {
        string_view header_line = sv_chop_char(request, '\n');
        if (header_line.size > 0 && header_line.data[header_line.size - 1] == '\r') {
            header_line.size--;
        }
        if (header_line.size == 0) break;

        string_view key = sv_chop_char(&header_line, ':');
        if (header_line.size > 0 && header_line.data[0] == ' ') {
            sv_chop(&header_line, 1);
        }
        string_view value = header_line;

        if (sv_equals(key, SV_LITERAL("User-Agent"), true)) {
            user_agent = value;
        } else if (sv_equals(key, SV_LITERAL("Content-Length"), true)) {
            *content_length = strtoul(value.data, NULL, 10);
        }
    }

    return user_agent;
}

string_view parse_http_request(const char *raw_request, size_t raw_request_len,
                              string_view *method, string_view *path, string_view *protocol,
                              string_view *user_agent, size_t *content_length) {
    string_view temp = sv_create(raw_request, raw_request_len);
    size_t initial_size = temp.size;

    parse_request_line(&temp, method, path, protocol);
    size_t request_line_consumed = initial_size - temp.size;

    initial_size = temp.size;
    *user_agent = parse_headers(&temp, content_length);
    size_t headers_consumed = initial_size - temp.size;

    size_t total_consumed = request_line_consumed + headers_consumed;
    return sv_create(raw_request + total_consumed, raw_request_len - total_consumed);
}