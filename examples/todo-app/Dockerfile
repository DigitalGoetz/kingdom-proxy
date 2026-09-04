# No npm dependencies (see server.js), so nothing to install -- just run it.
FROM node:22-alpine

WORKDIR /app
COPY server.js .

EXPOSE 8080
HEALTHCHECK --interval=10s --timeout=3s CMD wget -qO- http://127.0.0.1:8080/api/healthz || exit 1

CMD ["node", "server.js"]
