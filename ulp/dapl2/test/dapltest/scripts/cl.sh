#!/bin/sh
#
# Copyright (c) 2002-2005, Network Appliance, Inc. All rights reserved.
#
# This Software is licensed under one of the following licenses:
#
# 1) under the terms of the "Common Public License 1.0" a copy of which is
#    in the file LICENSE.txt in the root directory. The license is also
#    available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/cpl.php.
#
# 2) under the terms of the "The BSD License" a copy of which is in the file
#    LICENSE2.txt in the root directory. The license is also available from
#    the Open Source Initiative, see
#    http://www.opensource.org/licenses/bsd-license.php.
#
# 3) under the terms of the "GNU General Public License (GPL) Version 2" a 
#    copy of which is in the file LICENSE3.txt in the root directory. The 
#    license is also available from the Open Source Initiative, see
#    http://www.opensource.org/licenses/gpl-license.php.
#
# Licensee has the right to choose one of the above licenses.
#
# Redistributions of source code must retain the above copyright
# notice and one of the license notices.
#
# Redistributions in binary form must reproduce both the above copyright
# notice, one of the license notices in the documentation
# and/or other materials provided with the distribution.
#
# Sample client invocation
#
#
me=`basename $0`
case $# in      
0)      host=dat-linux3
        device=0 ;;
1)      host=$1
        device=0 ;;
2)      host=$1
        device=$2 ;;
*)      echo Usage: $me '[hostname [device] ]' 1>&2 ; exit 1;;
esac
#
#
# ./dapltest -T T -V -d -t 2 -w 2 -i 1000111 -s ${host} -D ${device} \ 
#           client RW  4096 1    server RW  2048 4     \
#           client RR  1024 2    server RR  2048 2     \
#           client SR  1024 3 -f server SR   256 3 -f

  ./dapltest -T T -P    -d -t 2 -w 2 -i 1024 -s ${host} -D ${device} \
            client RW  4096 1    server RW  2048 4     \
            client RR  1024 2    server RR  2048 2     \
            client SR  1024 3 -f server SR   256 3 -f

#dapltest -T T -d -s ${host} -D ${device} -i 10000 -t 1 -w 1 \
#	client SR 256 					   \
#	server SR 256
