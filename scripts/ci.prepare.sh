#!/usr/bin/env bash

for i in "$@"; do
  case $i in
    -p|--platform)
      PLATFORM="$2"
      shift # past argument
      shift # past value
      ;;
    -v|--version)
      VERSION="$2"
      shift # past argument
      shift # past value
      ;;
    -f|--flavor)
      FLAVOR=YES
      shift # past argument
      shift # past value
      ;;
    -*|--*)
      echo "Unknown option $1"
      exit 1
      ;;
    *)
      ;;
  esac
done

echo "PLATFORM  = ${PLATFORM}"
echo "VERSION   = ${VERSION}"
echo "FLAVOR    = ${FLAVOR}"
echo "FLAVOR    = $(dirname $0)"

wget https://github.com/ossappscollective/OSS-DocumentScanner/releases/download/dev_resources/${PLATFORM}.zip
unzip -o ${PLATFORM}.zip