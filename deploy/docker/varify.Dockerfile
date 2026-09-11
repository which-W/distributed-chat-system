FROM node:22-bookworm-slim
WORKDIR /app
COPY VarifyServer/package*.json ./
RUN npm ci --omit=dev
COPY VarifyServer/ ./
COPY proto/ /proto/
RUN node -e "require('./proto')"
CMD ["node", "server.js"]
