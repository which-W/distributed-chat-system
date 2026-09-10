FROM node:22-bookworm-slim
WORKDIR /app
COPY VarifyServer/package*.json ./
RUN npm ci --omit=dev
COPY VarifyServer/ ./
CMD ["node", "server.js"]
