/** @type {import('next').NextConfig} */
module.exports = {
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
            key: 'Content-Security-Policy',
            value: "default-src http: 'unsafe-eval' 'unsafe-inline'"
          }
        ]
      }
    ]
  },
  reactStrictMode: true
}
