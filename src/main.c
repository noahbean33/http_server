/*
 * HTTP/1.1 Server Implementation in C
 * 
 * This server supports:
 * - Concurrent connections using pthreads
 * - Multiple HTTP endpoints (/, /echo/, /user-agent, /files/)
 * - GET and POST methods
 * - File serving and creation
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

/**
 * Structure to pass data to each client handler thread
 * Contains the client socket file descriptor and optional directory path
 */
typedef struct {
	int client_fd;        // Client socket file descriptor
	char *directory;      // Directory for file operations (NULL if not specified)
} client_data_t;

/**
 * Extract the value of a specific HTTP header from the request
 * 
 * @param headers - Pointer to the headers section of the HTTP request
 * @param header_name - Name of the header to extract (e.g., "User-Agent")
 * @return Dynamically allocated string containing the header value, or NULL if not found
 *         Caller is responsible for freeing the returned string
 */
char * extract_header_value(char *headers, const char *header_name) 
{
	char *line = headers;
	char * header_start;
	
	// Search for the header name in the headers section
	while ((line = strstr(line, header_name)) != NULL)
	{
		// Skip past the header name
		header_start = line + strlen(header_name);
		
		// Skip whitespace and colon
		while(*header_start == ' ' || *header_start == ':')
		{
			header_start++;
		}
		
		// Find the end of the header value (marked by \r\n)
		char *header_end = strstr(header_start, "\r\n");
		if (header_end) {
			// Allocate memory and copy the header value
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

/**
 * Thread function to handle individual client connections
 * Parses HTTP requests and routes to appropriate endpoint handlers
 * 
 * @param arg - Pointer to client_data_t structure containing client info
 * @return NULL when complete
 */
void *handle_client(void *arg) {
	// Extract client data from thread argument
	client_data_t *data = (client_data_t *)arg;
	int client_fd = data->client_fd;
	char *directory = data->directory;
	free(data);
	
	// Read the HTTP request from the client
	char request[1024];
	ssize_t bytes_read = recv(client_fd, request, sizeof(request), 0);
	
	// Handle empty or failed reads
	if (bytes_read <= 0) {
		close(client_fd);
		return NULL;
	}
	
	// Variables for parsing the HTTP request
	char *http_method = NULL;    // GET, POST, etc.
	char *http_path = NULL;      // /echo/hello, /files/test, etc.
	char *saveptr = NULL;        // For strtok_r (headers section)
	char *line_saveptr = NULL;   // For strtok_r (request line)
	
	// Parse the request line: "METHOD PATH HTTP/1.1"
	char *request_line = strtok_r(request, "\r\n", &saveptr);
	http_method = strtok_r(request_line, " ", &line_saveptr);
	http_path = strtok_r(NULL, " ", &line_saveptr);
	
	/* ========== ROUTING ========== */
	
	// Endpoint: GET / - Returns a simple 200 OK response
	if (strcmp(http_path, "/") == 0)
	{
		char *response = "HTTP/1.1 200 OK\r\n\r\n";
		send(client_fd, response, strlen(response), 0);
	}
	
	// Endpoint: GET /echo/{string} - Echoes back the string from the URL
	else if (strncmp(http_path, "/echo/", 6) == 0)
	{
		// Extract the string after "/echo/"
		char *content = http_path + 6;
		size_t content_len = strlen(content);
		
		// Build and send HTTP response with the echoed content
		char response[1024];
		snprintf(response, sizeof(response), "HTTP/1.1 200 OK\r\n" "Content-Type: text/plain\r\n"
		"Content-Length: %zu\r\n" "\r\n" "%s", content_len, content);
		send(client_fd, response, strlen(response), 0);
	}
	// Endpoint: GET /user-agent - Returns the User-Agent header value
	else if (strcmp(http_path, "/user-agent") == 0)
	{
		// Extract the User-Agent header from the request
		char *user_agent = extract_header_value(saveptr, "User-Agent");
		
		if (user_agent) {
			// Build and send response with User-Agent value
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
			// User-Agent header not found
			char *response = "HTTP/1.1 400 Bad Request\r\n\r\n";
			send(client_fd, response, strlen(response), 0);
		}
	}
	// Endpoint: /files/{filename} - File serving (GET) and creation (POST)
	else if (strncmp(http_path, "/files/", 7) == 0 && directory != NULL)
	{
		// Extract filename from path
		char *filename = http_path + 7;
		
		// Build full filepath, handling directory paths with or without trailing slash
		char filepath[1024];
		size_t dir_len = strlen(directory);
		if (dir_len > 0 && directory[dir_len - 1] == '/') {
			snprintf(filepath, sizeof(filepath), "%s%s", directory, filename);
		} else {
			snprintf(filepath, sizeof(filepath), "%s/%s", directory, filename);
		}
		
		// GET /files/{filename} - Serve file contents
		if (strcmp(http_method, "GET") == 0)
		{
			FILE *file = fopen(filepath, "rb");
			if (file) {
				// Get file size
				fseek(file, 0, SEEK_END);
				long file_size = ftell(file);
				fseek(file, 0, SEEK_SET);
				
				// Read entire file into memory
				char *file_content = malloc(file_size);
				fread(file_content, 1, file_size, file);
				fclose(file);
				
				// Send HTTP response with file contents
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
				// File not found
				char *response = "HTTP/1.1 404 Not Found\r\n\r\n";
				send(client_fd, response, strlen(response), 0);
			}
		}
		// POST /files/{filename} - Create new file with request body
		else if (strcmp(http_method, "POST") == 0)
		{
			// Extract request body (located after "\r\n\r\n")
			char *body = strstr(saveptr, "\r\n\r\n");
			if (body) {
				body += 4; // Skip the "\r\n\r\n" separator
			}
			
			// Get Content-Length header to know how many bytes to write
			char *content_length_str = extract_header_value(saveptr, "Content-Length");
			if (content_length_str && body) {
				size_t content_length = atoi(content_length_str);
				free(content_length_str);
				
				// Create and write file
				FILE *file = fopen(filepath, "wb");
				if (file) {
					// Write exact number of bytes from body (binary safe)
					fwrite(body, 1, content_length, file);
					fclose(file);
					
					// Send 201 Created response
					char *response = "HTTP/1.1 201 Created\r\n\r\n";
					send(client_fd, response, strlen(response), 0);
				} else {
					// Failed to create file
					char *response = "HTTP/1.1 500 Internal Server Error\r\n\r\n";
					send(client_fd, response, strlen(response), 0);
				}
			} else {
				// Missing Content-Length header or body
				char *response = "HTTP/1.1 400 Bad Request\r\n\r\n";
				send(client_fd, response, strlen(response), 0);
			}
		}
	}
	// Default: 404 Not Found for all other paths
	else
	{
		char *response = "HTTP/1.1 404 Not Found\r\n\r\n";
		send(client_fd, response, strlen(response), 0);
	}
	
	// Close client connection and exit thread
	close(client_fd);
	return NULL;
}

/**
 * Main function - Sets up the HTTP server and handles incoming connections
 * 
 * Usage: ./server [--directory <path>]
 *   --directory: Optional directory path for file operations
 */
int main(int argc, char *argv[]) {
	// Disable output buffering for immediate console output
	setbuf(stdout, NULL);
 	setbuf(stderr, NULL);

	// Parse command-line arguments for optional --directory flag
	char *directory = NULL;
	if (argc >= 3 && strcmp(argv[1], "--directory") == 0) {
		directory = argv[2];
	}

	printf("HTTP Server starting...\n");

	// Create TCP socket
	int server_fd;
	
	server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (server_fd == -1) {
		printf("Socket creation failed: %s...\n", strerror(errno));
		return 1;
	}
	
	// Set SO_REUSEADDR to prevent "Address already in use" errors on restart
	int reuse = 1;
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
		printf("SO_REUSEADDR failed: %s \n", strerror(errno));
		return 1;
	}
	
	// Configure server address: listen on all interfaces (0.0.0.0) at port 4221
	struct sockaddr_in serv_addr = { .sin_family = AF_INET ,
									 .sin_port = htons(4221),
									 .sin_addr = { htonl(INADDR_ANY) },
									};
	
	// Bind socket to address and port
	if (bind(server_fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) != 0) {
		printf("Bind failed: %s \n", strerror(errno));
		return 1;
	}
	
	// Listen for incoming connections (backlog of 5)
	int connection_backlog = 5;
	if (listen(server_fd, connection_backlog) != 0) {
		printf("Listen failed: %s \n", strerror(errno));
		return 1;
	}
	
	printf("Server listening on port 4221...\n");
	printf("Waiting for clients to connect...\n");
	
	// Main server loop - accept and handle connections concurrently
	while (1) {
		struct sockaddr_in client_addr;
		socklen_t client_addr_len = sizeof(client_addr);
		
		// Accept incoming connection (blocks until client connects)
		int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
		if (client_fd == -1) {
			printf("Accept failed: %s\n", strerror(errno));
			continue;
		}
		
		printf("Client connected\n");
		
		// Prepare data for the client handler thread
		pthread_t thread_id;
		client_data_t *data = malloc(sizeof(client_data_t));
		data->client_fd = client_fd;
		data->directory = directory;
		
		// Create new thread to handle this client
		if (pthread_create(&thread_id, NULL, handle_client, data) != 0) {
			printf("Failed to create thread\n");
			close(client_fd);
			free(data);
			continue;
		}
		
		// Detach thread so it cleans up automatically when done
		pthread_detach(thread_id);
	}
	
	close(server_fd);
	return 0;
}
