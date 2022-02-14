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
              'Amu7sW/oEH3ZqF6SQcPOYVpF9KYNHShFxN1GzM5DY0QW6NwGnbe2kE/YyeQdkSD+kZWhmRnUwQT85zvOA5WYfgAAAABJeyJvcmlnaW4iOiJodHRwOi8vbG9jYWxob3N0OjMwMDAiLCJmZWF0dXJlIjoiV2ViR1BVIiwiZXhwaXJ5IjoxNjUyODMxOTk5fQ=='
          }
        ]
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
