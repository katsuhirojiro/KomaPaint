.pragma library

// Draw a checkerboard into any Canvas-like context. Used for the canvas
// document placeholder and layer thumbnail backgrounds.

function paintCheckerboard(ctx, width, height, cellSize, colorA, colorB) {
    ctx.save();
    ctx.fillStyle = colorA;
    ctx.fillRect(0, 0, width, height);
    ctx.fillStyle = colorB;
    var cs = cellSize || 12;
    for (var y = 0; y < height; y += cs) {
        for (var x = 0; x < width; x += cs) {
            if (((x / cs) + (y / cs)) % 2 === 0) {
                ctx.fillRect(x, y, cs, cs);
            }
        }
    }
    ctx.restore();
}
