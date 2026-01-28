const test = require('brittle')
const ico = require('.')

const fixture = require('./test/fixtures/computer.ico', {
  with: { type: 'binary' }
})

test('decode', (t) => {
  const result = ico.decode(fixture)

  t.ok(result.width > 0)
  t.ok(result.height > 0)
  t.ok(Buffer.isBuffer(result.data))
  t.ok(Array.isArray(result.sizes))
  t.ok(result.sizes.length > 0)
})

test('decode with size option', (t) => {
  const result = ico.decode(fixture, { size: 16 })

  t.ok(result.width > 0)
  t.ok(result.height > 0)
  t.ok(Buffer.isBuffer(result.data))
})

test('encode throws', (t) => {
  t.exception(() => ico.encode(Buffer.alloc(0)))
})

test('encodeAnimated throws', (t) => {
  t.exception(() => ico.encodeAnimated([]))
})

test('invalid ico throws', (t) => {
  t.exception(() => ico.decode(Buffer.alloc(0)))
})
