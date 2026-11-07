FROM nodered/node-red:5.0.8

# Dashboard 2.0 is baked into the image so a fresh clone works without
# installing anything from the palette manager.
RUN npm install --no-audit --no-fund --omit=dev @flowfuse/node-red-dashboard@1.34.0
