/** @type {import('next').NextConfig} */
module.exports = {
  // target: 'serverless',
  // async rewrites () {
  //   return [
  //     {
  //       source: '/:any*',
  //       destination: '/'
  //     }
  //   ]
  // },
  env: {
    VERSION: process.env.VERSION
  },
  async headers () {
    return [
      {
        source: '/:path*',
        headers: [
          {
            key: 'Cross-Origin-Embedder-Policy',
            value: 'require-corp'
          },
          {
            key: 'Cross-Origin-Opener-Policy',
            value: 'same-origin'
          },
          {
            key: 'Access-Control-Allow-Origin',
            value: '*'
          },
          {
            key: 'origin-trial',
            value:
              'AjPwILqou86MNqXlfZc0tZycsl9U9sV/uI2ti0RK1/w0kT3/l35O3zugkEb31z1gKbxnakvZahtfWf9h42buSA4AAABJeyJvcmlnaW4iOiJodHRwOi8vbG9jYWxob3N0OjMwMDAiLCJmZWF0dXJlIjoiV2ViR1BVIiwiZXhwaXJ5IjoxNjYzNzE4Mzk5fQ=='
          }
        ]
      }
    ]
  },
  async rewrites () {
    return [
      {
        source: '/api/:path*',
        destination: 'http://mirakc:40772/api/:path*'
      }
    ]
  },
  webpack: (config, { webpack }) => {
    const experiments = config.experiments || {}
    config.experiments = {
      ...experiments,
      asyncWebAssembly: true,
      syncWebAssembly: true
    }
    config.output.webassemblyModuleFilename = 'static/wasm/[modulehash].wasm'
    // config.output.assetModuleFilename = `static/[hash][ext]`
    // config.output.publicPath = `/_next/`
    // config.module.rules.push({
    //   test: /\.wasm$/,
    //   // type: 'webassembly/async'
    //   loader: 'raw-loader'
    // })
    return config
  },
  reactStrictMode: true
}
