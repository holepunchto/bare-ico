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

test('rejects BMP-in-ICO with oversized dimensions', (t) => {
  // bmp_width chosen so that `bmp_width * bmp_bpp` overflows int32, which
  // pre-fix wrapped row_size / allocation size to a tiny value while the
  // per-row loops still ran for the full un-wrapped width.
  const buf = Buffer.alloc(6 + 16 + 40)
  buf.writeUInt16LE(0, 0) // reserved
  buf.writeUInt16LE(1, 2) // type
  buf.writeUInt16LE(1, 4) // count
  buf.writeUInt16LE(1, 10) // planes
  buf.writeUInt16LE(32, 12) // bit_count
  buf.writeUInt32LE(40, 14) // bytes_in_res
  buf.writeUInt32LE(22, 18) // image_offset
  buf.writeUInt32LE(40, 22) // header_size
  buf.writeInt32LE(0x10000000, 26) // bmp_width — overflow trigger
  buf.writeInt32LE(2, 30) // bmp_height
  buf.writeUInt16LE(1, 34) // planes
  buf.writeUInt16LE(32, 36) // bpp
  buf.writeUInt32LE(0, 38) // compression

  t.exception(() => ico.decode(buf))
})

test('rejects 8-bit BMP-in-ICO with undersized palette region', (t) => {
  // 8-bit decoder indexes palette[i*4 + ...] for i up to 255, expecting a
  // 256-entry (1024-byte) palette after the DIB header. If the file only
  // provides space for a smaller palette, those reads would walk off the
  // input buffer onto the heap.
  const headerSize = 40
  const undersizedPalette = 16 * 4
  const pixelBytes = 8
  const bmpSize = headerSize + undersizedPalette + pixelBytes

  const buf = Buffer.alloc(6 + 16 + bmpSize)
  buf.writeUInt16LE(0, 0)
  buf.writeUInt16LE(1, 2)
  buf.writeUInt16LE(1, 4)
  buf[6] = 4
  buf[7] = 2
  buf.writeUInt16LE(1, 10)
  buf.writeUInt16LE(8, 12)
  buf.writeUInt32LE(bmpSize, 14)
  buf.writeUInt32LE(22, 18)
  buf.writeUInt32LE(headerSize, 22)
  buf.writeInt32LE(4, 26)
  buf.writeInt32LE(2, 30)
  buf.writeUInt16LE(1, 34)
  buf.writeUInt16LE(8, 36) // bpp = 8
  buf.writeUInt32LE(0, 38)

  t.exception(() => ico.decode(buf))
})

test('decodes a 32-bit BMP-in-ICO', (t) => {
  const result = ico.decode(bmpInIco({ width: 2, height: 2, bpp: 32 }))

  t.is(result.width, 2)
  t.is(result.height, 2)
})

test('decodes an 8-bit BMP-in-ICO', (t) => {
  const result = ico.decode(
    bmpInIco({ width: 2, height: 2, bpp: 8, palette: Buffer.alloc(256 * 4) })
  )

  t.is(result.width, 2)
  t.is(result.height, 2)
})

test('rejects BMP-in-ICO with unsupported bit depth', (t) => {
  // Valid header + in-bounds pixel data but bpp = 16 reaches the rgba_data
  // allocation, then the unsupported-format branch that must free(rgba_data).
  t.exception(
    () => ico.decode(bmpInIco({ width: 2, height: 2, bpp: 16 })),
    /Unsupported BMP format/
  )
})

test('rejects ICO entry with offset+size that wraps uint32', (t) => {
  // image_offset + bytes_in_res wraps in uint32: 0xFFFFFFFE + 4 = 2, which
  // pre-fix bypassed the `offset + size > ico_len` bounds check.
  const buf = Buffer.alloc(6 + 16)
  buf.writeUInt16LE(0, 0)
  buf.writeUInt16LE(1, 2)
  buf.writeUInt16LE(1, 4)
  buf.writeUInt16LE(1, 10)
  buf.writeUInt16LE(32, 12)
  buf.writeUInt32LE(4, 14) // bytes_in_res
  buf.writeUInt32LE(0xfffffffe, 18) // image_offset — wrap trigger

  t.exception(() => ico.decode(buf))
})

// Helpers

// Build a single-entry ICO wrapping a BMP-in-ICO image. `height` is the real
// image height; the DIB header stores it doubled (image + AND mask).
function bmpInIco({ width, height, bpp, palette = Buffer.alloc(0), pixels }) {
  const headerSize = 40
  const rowSize = Math.floor((width * bpp + 31) / 32) * 4
  const pixelData = pixels ?? Buffer.alloc(rowSize * height)
  const bmp = Buffer.concat([Buffer.alloc(headerSize), palette, pixelData])

  bmp.writeUInt32LE(headerSize, 0)
  bmp.writeInt32LE(width, 4)
  bmp.writeInt32LE(height * 2, 8)
  bmp.writeUInt16LE(1, 12)
  bmp.writeUInt16LE(bpp, 14)
  bmp.writeUInt32LE(0, 16)

  const buf = Buffer.alloc(6 + 16 + bmp.length)
  buf.writeUInt16LE(0, 0)
  buf.writeUInt16LE(1, 2)
  buf.writeUInt16LE(1, 4)
  buf[6] = width & 0xff
  buf[7] = (height * 2) & 0xff
  buf.writeUInt16LE(1, 10)
  buf.writeUInt16LE(bpp, 12)
  buf.writeUInt32LE(bmp.length, 14)
  buf.writeUInt32LE(22, 18)
  bmp.copy(buf, 22)

  return buf
}
