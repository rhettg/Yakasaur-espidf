# Start with the official Caddy image
FROM caddy:latest

# Copy the Caddyfile to the container
COPY Caddyfile /etc/caddy/Caddyfile

# Set the entrypoint to run Caddy
ENTRYPOINT ["caddy", "run", "--config", "/etc/caddy/Caddyfile"]
