'use strict';

// xer3 — Node.js bindings for the XER engine (xer.node, built by CMake).
//
//   const xer = require('xer3');
//   xer.info();
//   xer.image.info('photo.png');
//   xer.image.decode('photo.png', { rgba: true });
//   xer.image.save({ data, width, height, format: 'rgba8' }, 'out.png');
//   xer.audio.info('song.mp3');
//   xer.audio.convert('in.wav', 'out.flac', { sampleRate: 48000 });

const path = require('path');
const fs = require('fs');

const candidates = [
  // CMake build output (repo layout: <repo>/build/bin/xer.node)
  path.join(__dirname, '..', 'build', 'bin', 'xer.node'),
  // Multi-config generators may place it under a config dir
  path.join(__dirname, '..', 'build', 'Release', 'xer.node'),
  path.join(__dirname, '..', 'build', 'bin', 'Release', 'xer.node'),
  // In case the module was copied next to index.js
  path.join(__dirname, 'xer.node'),
];

let addon = null;
let lastError = null;
for (const candidate of candidates) {
  if (!fs.existsSync(candidate)) continue;
  try {
    addon = require(candidate);
    break;
  } catch (err) {
    lastError = err;
  }
}

if (addon === null) {
  const searched = candidates.map((c) => `  ${c}`).join('\n');
  throw new Error(
    'xer3: native module xer.node not found or failed to load.\n' +
      'Build it with: cmake -B build -DXER_BUILD_NODE_ADDON=ON && ' +
      'cmake --build build --config Release\n' +
      'Searched:\n' + searched +
      (lastError ? `\nLoad error: ${lastError.message}` : '')
  );
}

module.exports = addon;
