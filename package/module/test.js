// Test file for XER Native bindings

const xer = require('./index.js');

console.log('Testing XER Native Bindings\n');

// Test Helper module
console.log('=== Helper Module ===');
try {
  const buffer = xer.helper.buffer.fromString('Hello World');
  console.log('✓ buffer.fromString:', buffer.length, 'bytes');

  const str = xer.helper.buffer.toString(buffer);
  console.log('✓ buffer.toString:', str);

  const hex = xer.helper.buffer.hexEncode(buffer);
  console.log('✓ buffer.hexEncode:', hex);

  const decoded = xer.helper.buffer.hexDecode(hex);
  console.log('✓ buffer.hexDecode:', decoded.length, 'bytes');
} catch (e) {
  console.log('✗ Helper error:', e.message);
}

// Test Cache module
console.log('\n=== Cache Module ===');
try {
  const instance = xer.cache.instance();
  console.log('✓ cache.instance:', instance);
} catch (e) {
  console.log('✗ Cache error:', e.message);
}

// Test Audio module
console.log('\n=== Audio Module ===');
try {
  const engine = new xer.audio.Engine();
  console.log('✓ audio.Engine created');

  try {
    engine.initialize(48000, 512, 2);
    console.log('✓ audio.Engine initialize');
  } catch (e) {
    console.log('⚠ audio.Engine initialize (expected - not linked):', e.message);
  }
} catch (e) {
  console.log('✗ Audio error:', e.message);
}

// Test Async IO module
console.log('\n=== Async IO Module ===');
try {
  const reader = new xer.asyncIO.FileReader();
  console.log('✓ asyncIO.FileReader created');

  try {
    reader.openFile('/tmp/test.bin');
    console.log('✓ asyncIO.FileReader openFile');
  } catch (e) {
    console.log('⚠ asyncIO.FileReader openFile (expected - not linked):', e.message);
  }
} catch (e) {
  console.log('✗ Async IO error:', e.message);
}

// Test Flux module
console.log('\n=== Flux Module ===');
try {
  const core = new xer.flux.Core();
  console.log('✓ flux.Core created');

  try {
    core.initialize();
    console.log('✓ flux.Core initialize');
  } catch (e) {
    console.log('⚠ flux.Core initialize (expected - not linked):', e.message);
  }

  const size = xer.flux.terminal.defaultSize();
  console.log('✓ flux.terminal.defaultSize:', size);

  const detected = xer.flux.terminal.detectSize();
  console.log('✓ flux.terminal.detectSize:', detected);
} catch (e) {
  console.log('✗ Flux error:', e.message);
}

console.log('\n=== Test Complete ===');
console.log('Note: Most APIs are stubs. Link against XER libraries for full functionality.');
