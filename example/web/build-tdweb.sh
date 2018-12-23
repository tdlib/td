#!/bin/sh

cd tdweb
npm install || exit 1
npm run build || exit 1
cd ..
