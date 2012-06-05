#!/bin/bash

# Make sure we're running the script from the proper directory.
if [ ! -f AUTHORS ]; then
    echo "Must be run from the package root"
    exit 1
fi

# Clean up the directory.
if [ -f Makefile ]; then
    make distclean
elif [ -f src/aws-s3fs ]; then
    make clean
fi

# Create a tarball name based on the package directory.
DIR=`basename $PWD`
TARBALLNAME=adf
PACKAGE=`echo ${DIR} | sed -n "s/-\([0-9]\+\.[0-9]\+\)//p"`
VERSION=`echo ${DIR} | sed -n "s/${PACKAGE}-\([0-9]\+\.[0-9]\+\)/\1/p"`

cd ..
tar -cjf ${PACKAGE}_${VERSION}.orig.tar.xz --auto-compress --exclude=${DIR}/debian/* --exclude=${DIR}/.git/* --exclude=${DIR}/.gitignore ${DIR}
