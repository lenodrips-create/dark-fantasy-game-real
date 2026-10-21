// Dark Fantasy Game – Stickman prototype
// WASD movement. Real level will come from the original Blender file later.

const canvas = document.createElement('canvas');
document.body.appendChild(canvas);
const ctx = canvas.getContext('2d');

function resize() {
  canvas.width = window.innerWidth;
  canvas.height = window.innerHeight;
}
window.addEventListener('resize', resize);
resize();

const player = {
  x: 0,
  y: 0,
  speed: 4.5,
  facing: 1
};

const keys = {};
window.addEventListener('keydown', e => { keys[e.code] = true; e.preventDefault(); });
window.addEventListener('keyup', e => { keys[e.code] = false; });

function update() {
  let dx = 0, dy = 0;
  if (keys['KeyW'] || keys['ArrowUp'])    dy -= 1;
  if (keys['KeyS'] || keys['ArrowDown'])  dy += 1;
  if (keys['KeyA'] || keys['ArrowLeft'])  dx -= 1;
  if (keys['KeyD'] || keys['ArrowRight']) dx += 1;

  if (dx !== 0 || dy !== 0) {
    const len = Math.hypot(dx, dy);
    player.x += (dx / len) * player.speed;
    player.y += (dy / len) * player.speed;
    if (dx !== 0) player.facing = dx > 0 ? 1 : -1;
  }
}

function drawStickman(screenX, screenY) {
  ctx.save();
  ctx.translate(screenX, screenY);
  ctx.scale(player.facing, 1);
  ctx.strokeStyle = '#e8e8e8';
  ctx.lineWidth = 3.5;
  ctx.lineCap = 'round';
  ctx.lineJoin = 'round';

  // Head
  ctx.beginPath();
  ctx.arc(0, -30, 13, 0, Math.PI * 2);
  ctx.stroke();

  // Body
  ctx.beginPath();
  ctx.moveTo(0, -17);
  ctx.lineTo(0, 20);
  ctx.stroke();

  // Arms
  ctx.beginPath();
  ctx.moveTo(-20, 2);
  ctx.lineTo(0, 6);
  ctx.lineTo(20, 2);
  ctx.stroke();

  // Legs
  ctx.beginPath();
  ctx.moveTo(0, 20);
  ctx.lineTo(-15, 48);
  ctx.moveTo(0, 20);
  ctx.lineTo(15, 48);
  ctx.stroke();

  ctx.restore();
}

function drawGround() {
  ctx.strokeStyle = '#2a2a2a';
  ctx.lineWidth = 1;
  const spacing = 70;
  const ox = ((player.x % spacing) + spacing) % spacing;
  const oy = ((player.y % spacing) + spacing) % spacing;

  for (let x = -ox; x < canvas.width + spacing; x += spacing) {
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.lineTo(x, canvas.height);
    ctx.stroke();
  }
  for (let y = -oy; y < canvas.height + spacing; y += spacing) {
    ctx.beginPath();
    ctx.moveTo(0, y);
    ctx.lineTo(canvas.width, y);
    ctx.stroke();
  }

  // subtle center cross
  ctx.strokeStyle = '#333';
  ctx.beginPath();
  ctx.moveTo(canvas.width/2 - 20, canvas.height/2);
  ctx.lineTo(canvas.width/2 + 20, canvas.height/2);
  ctx.moveTo(canvas.width/2, canvas.height/2 - 20);
  ctx.lineTo(canvas.width/2, canvas.height/2 + 20);
  ctx.stroke();
}

function loop() {
  update();
  ctx.fillStyle = '#111';
  ctx.fillRect(0, 0, canvas.width, canvas.height);
  drawGround();
  // camera follows player
  drawStickman(canvas.width / 2, canvas.height / 2);
  requestAnimationFrame(loop);
}

loop();
