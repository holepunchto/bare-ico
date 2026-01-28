# bare-ico

ICO support for Bare.

```
npm i bare-ico
```

## Usage

```js
const ico = require('bare-ico')

const image = require('./my-image.ico', { with: { type: 'binary' } })

const decoded = ico.decode(image)
// {
//   width: 200,
//   height: 400,
//   data: <Buffer>,
//   sizes: [16, 32, 48, 256]
// }
```

## License

Apache-2.0
