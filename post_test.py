# Import the necessary modules from Python's standard library
from http.server import HTTPServer, BaseHTTPRequestHandler

# Define the server's host and port
HOST = "0.0.0.0"
PORT = 8000

# Create a custom request handler by subclassing BaseHTTPRequestHandler
class SimpleServer(BaseHTTPRequestHandler):
    """
    A simple HTTP request handler that prints the body of POST requests.
    """
    def do_POST(self):
        """Handle POST requests."""
        # 1. Get the size of the incoming data
        content_length = int(self.headers['Content-Length'])

        # 2. Read the data from the request
        post_data_bytes = self.rfile.read(content_length)

        # 3. Decode the bytes to a string (assuming UTF-8 encoding)
        post_data_str = post_data_bytes.decode("utf-8")

        # 4. Print the received data to the server's console
        print("\n----- POST DATA RECEIVED -----")
        print(post_data_str)
        print("----------------------------\n")

        # 5. Send a success response back to the client
        self.send_response(200)  # 200 OK
        self.send_header("Content-type", "text/plain")
        self.end_headers()
        self.wfile.write(b"POST request successfully received!")

    def do_GET(self):
        """Handle GET requests with a simple message."""
        self.send_response(200) # 200 OK
        self.send_header("Content-type", "text/html")
        self.end_headers()
        self.wfile.write(b"<html><body><h1>Server is running!</h1><p>Send a POST request to this endpoint to see it in the console.</p></body></html>")


# Create and run the server
if __name__ == "__main__":
    # Create an HTTP server instance with the defined host, port, and our custom handler
    web_server = HTTPServer((HOST, PORT), SimpleServer)
    print(f"Server started at http://{HOST}:{PORT}")
    print("Press Ctrl+C to stop the server.")

    try:
        # Start the server and keep it running until interrupted
        web_server.serve_forever()
    except KeyboardInterrupt:
        # Handle graceful shutdown on Ctrl+C
        pass

    web_server.server_close()
    print("\nServer stopped.")
