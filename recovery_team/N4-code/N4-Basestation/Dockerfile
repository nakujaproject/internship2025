FROM node:20-alpine AS build

WORKDIR /app

COPY package*.json ./
RUN npm install

COPY . .
RUN npm run build


FROM node:20-alpine

WORKDIR /app

RUN apk add --no-cache python3 py3-pip mosquitto

RUN pip3 install --break-system-packages --no-cache-dir pyserial paho-mqtt

COPY --from=build /app/dist ./dist
COPY . .

RUN npm install --omit=dev

EXPOSE 3000 5173 8080 1883

CMD ["python3", "start_basestation.py"]
