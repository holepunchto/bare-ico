const binding = require('./binding')

exports.decode = function decode(ico, opts = {}) {
  const result = binding.decode(ico, opts)

  return {
    width: result.width,
    height: result.height,
    data: Buffer.from(result.data),
    sizes: result.sizes
  }
}

exports.encode = function encode(rgba, opts = {}) {
  throw new Error('ICO encoding not yet implemented')
}

exports.encodeAnimated = function encodeAnimated(frames, opts = {}) {
  throw new Error('Animated ICO not supported')
}
