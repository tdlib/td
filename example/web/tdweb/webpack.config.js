const path = require('path');
const CleanWebpackPlugin = require('clean-webpack-plugin');

module.exports = {
  entry: ['./src/index.js'],
  output: {
    filename: 'tdweb.js',
    path: path.resolve(__dirname, 'dist'),
    library: 'tdweb',
    libraryTarget: 'umd',
    umdNamedDefine: true,
    globalObject: 'this'
  },
  devServer: {
    static: './dist'
  },
  plugins: [
    // new HtmlWebpackPlugin(),
    new CleanWebpackPlugin({})
    //, new UglifyJSPlugin()
  ],
  optimization:{
    minimize: false, // <---- disables uglify.
  },
  module: {
    rules: [
      {
        test: /worker\.(js|jsx)$/,
        include: [path.resolve(__dirname, 'src')],
        use: [
          {
            loader: require.resolve('worker-loader')
          }
        ]
      },
      {
        test: /\.(js|jsx)$/,
        exclude: /prebuilt/,
        include: [path.resolve(__dirname, 'src')],
        use: [
          {
            loader: require.resolve('babel-loader')
          }
        ]
      },
      {
        test: /\.(wasm|mem)$/,
        include: [path.resolve(__dirname, 'src')],
        type: "javascript/auto",
        use: [
          {
            loader: require.resolve('file-loader')
          }
        ]
      }
    ]
  },
  performance: {
    maxAssetSize: 30000000
  },
  resolve: {
    fallback: {
      dgram: false,
      fs: false,
      net: false,
      tls: false,
      crypto: false,
      child_process: false
    },
    alias: {
      ws$: 'fs'
    }
  }
};
