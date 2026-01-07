#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

typedef struct {
	int client_fd;
	char *directory;
} client_data_t;

char * extract_header_value(char *headers, const char *header_name) 
{
	char *line = headers;
	char * header_start;
	while ((line = strstr(line, header_name)) != NULL)
	{
		header_start = line + strlen(header_name);
		while(*header_start == ' ' || *header_start == ':')
		{
			header_start++;
		}
		char *header_end = strstr(header_start, "\r\n");
		if (header_end) {
			size_t value_len = header_end - header_start;
			char *value = malloc(value_len + 1);
			strncpy(value, header_start, value_len);
			value[value_len] = '\0';
			return value;
		}
		line++;
	}
	return NULL;
}

void *handle_client(void *arg) {
	client_data_t *data = (client_data_t *)arg;
	int client_fd = data->client_fd;
	char *directory = data->directory;
	free(data);
	
	char request[1024];
	ssize_t bytes_read = recv(client_fd, request, sizeof(request), 0);
	
	if (bytes_read <= 0) {
		close(client_fd);
		return NULL;
	}
	
	char *http_method = NULL;
	char *http_path = NULL;
	char *saveptr = NULL;
	char *line_saveptr = NULL;
	
	http_method = strtok_r(request, "\r\n", &saveptr);
	http_path = strtok_r(http_method, " ", &line_saveptr);
	http_path = strtok_r(NULL, " ", &line_saveptr);
	
	if (strcmp(http_path, "/") == 0)
	{
		char *response = "HTTP/1.1 200 OK\r\n\r\n";
		send(client_fd, response, strlen(response), 0);
	}
	else if (strncmp(http_path, "/echo/", 6) == 0)
	{
		char *content = http_path + 6;
		size_t content_len = strlen(content);
		char response[1024];
		snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\n" "Content-Type: text/plain\r\n"
		"Content-Length: %zu\r\n" "\r\n" "%s", content_len, content);
		send(client_fd, response, strlen(response), 0);
	}
	else if (strcmp(http_path, "/user-agent") == 0)
	{
		char *user_agent = extract_header_value(saveptr, "User-Agent");
		
		if (user_agent) {
			size_t content_len = strlen(user_agent);
			char response[1024];
			snprintf(response, sizeof(response), 
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: text/plain\r\n"
				"Content-Length: %zu\r\n"
				"\r\n"
				"%s", 
				content_len, user_agent);
			send(client_fd, response, strlen(response), 0);
			free(user_agent);
		} else {
			char *response = "HTTP/1.1 400 Bad Request\r\n\r\n";
			send(client_fd, response, strlen(response), 0);
		}
	}
	else if (strncmp(http_path, "/files/", 7) == 0 && directory != NULL)
	{
		char *filename = http_path + 7;
		
		char filepath[1024];
		size_t dir_len = strlen(directory);
		if (dir_len > 0 && directory[dir_len - 1] == '/') {
			snprintf(filepath, sizeof(filepath), "%s%s", directory, filename);
		} else {
			snprintf(filepath, sizeof(filepath), "%s/%s", directory, filename);
		}
		
		FILE *file = fopen(filepath, "rb");
		if (file) {
			fseek(file, 0, SEEK_END);
			long file_size = ftell(file);
			fseek(file, 0, SEEK_SET);
			
			char *file_content = malloc(file_size);
			fread(file_content, 1, file_size, file);
			fclose(file);
			
			char response[2048];
			int header_len = snprintf(response, sizeof(response),
				"HTTP/1.1 200 OK\r\n"
				"Content-Type: application/octet-stream\r\n"
				"Content-Length: %ld\r\n"
				"\r\n",
				file_size);
			
			send(client_fd, response, header_len, 0);
			send(client_fd, file_content, file_size, 0);
			free(file_content);
		} else {
			char *response = "HTTP/1.1 404 Not Found\r\n\r\n";
			send(client_fd, response, strlen(response), 0);
		}
	}
	else
	{
		char *response = "HTTP/1.1 404 Not Found\r\n\r\n";
		send(client_fd, response, strlen(response), 0);
	}
	
	close(client_fd);
	return NULL;
}

int main(int argc, char *argv[]) {
	// Disable output buffering
	setbuf(stdout, NULL);
 	setbuf(stderr, NULL);

	// Parse command-line arguments for directory flag
	char *directory = NULL;
	if (argc >= 3 && strcmp(argv[1], "--directory") == 0) {
		directory = argv[2];
	}

	// You can use print statements as follows for debugging, they'll be visible when running tests.
	printf("Logs from your program will appear here!\n");

	int server_fd;
	
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	
	// Since the tester restarts your program quite often, setting SO_REUSEADDR
	// ensures that we don't run into 'Address already in use' errors
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	
	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(4221),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
	
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}
	
	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}
	
	printf("Waiting for clients to connect...\n");
	
	// Infinite loop to handle multiple concurrent connections
	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		
		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_fd == -1) {
			printf("Accept failed: %s\n", strerror(errno));
			continue;
		}
		
		printf("Client connected\n");
		
		// Create a new thread for this client
		pthread_t thread_id;
		client_data_t *data = malloc(sizeof(client_data_t));
		data->client_fd = client_fd;
		data->directory = directory;
		
		if (pthread_create(&thread_id, NULL, handle_client, data) != 0) {
			printf("Failed to create thread\n");
			close(client_fd);
			free(data);
			continue;
		}
		
		pthread_detach(thread_id);
	}
	
	close(server_fd);
	return 0;
}
