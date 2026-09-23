#! /bin/bash

# ===========================================================
# Copyright(C) 2023-2024 Advanced Micro Devices Inc.  All Rights Reserved.
# ===========================================================

# this script will install the python package given on the command line
# on a local directory only if the package are not already available

# Usage
#
# python_package.bash --install cv2:open-python==1.2.0
# eval $( python_package.bash --env )
#

# syntax for the package is:
# import_name:package_name==version
#  where import_name is the name used for import in python
#  package_name is the package to install using pip
#  version is a mandatory version to use

# the version is optional
# if the import_name and package_name are identical, the import_name is not
# needed

# for instance


if [ "$1" = "--local" ]
then
    # package will be installed in the local directory
    shift
else
    # but, by default, package are installed in the examples directory (the directory above this script)
    cd $( dirname "${BASH_SOURCE[0]}" )
    cd ..
fi

if [ "$1" = "--absolute" ]
then
    dir=$PWD
    shift
else
    dir="."
fi

userbase="$dir/$( PYTHONUSERBASE=.local python3 -m site --user-site )"
[ "$?" != 0 ] && echo "ERROR: python3 is not installed, execution outside of docker may not work" >&2 && exit 1

if [ "$PYTHONPATH" = "" ]
then
    export PYTHONPATH=$userbase
else
    export PYTHONPATH=$userbase:$PYTHONPATH
fi

# just return the environment
if [ "$1" = "--env" ]
then
    echo "export PYTHONPATH=$PYTHONPATH"
    exit 0
fi


[ "$1" != "--install" ] && echo "ERROR: invalid argument $@" && exit 1
shift


python_install_list=''
for dep in $*
do
    package=${dep%%==*}
    package=${package%%:*}
    version=${dep##*==}
    cmd="import $package"
    # yes, the test inside exit is != because true in shell is 0 while 0 is
    # false in python
    [ "$version" != "$dep" ] && cmd="$cmd ; exit(not $package.__version__.startswith('$version'))"
    python3 -c "$cmd" &> /dev/null || python_install_list="$python_install_list ${dep##*:}"
done

if [ "$python_install_list" != "" ]
then
    if ! python3 -m pip -V &> /dev/null
    then
        echo "Pip is not available on this system, please install python3-pip package. On Ubuntu, use:"
        echo "sudo apt install python3-pip"
        exit 1
    else
        echo "Updating pip"
        PYTHONUSERBASE=.local python3 -m pip install --user --upgrade pip || exit 1
        echo "Installing python dependencies: $python_install_list"
        PYTHONUSERBASE=.local python3 -m pip install --user $python_install_list || exit 1
    fi
fi

# vim: set expandtab sw=4:
