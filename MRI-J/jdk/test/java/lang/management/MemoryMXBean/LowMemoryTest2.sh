#
# Copyright 2004-2005 Sun Microsystems, Inc.  All Rights Reserved.
# DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
#
# This code is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 only, as
# published by the Free Software Foundation.
#
# This code is distributed in the hope that it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
# version 2 for more details (a copy is included in the LICENSE file that
# accompanied this code).
#
# You should have received a copy of the GNU General Public License version
# 2 along with this work; if not, write to the Free Software Foundation,
# Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
#
# Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
# CA 95054 USA or visit www.sun.com if you need additional information or
# have any questions.
#

#
# @test
# @bug     4982128
# @summary Test low memory detection of non-heap memory pool
#
# @run build LowMemoryTest2 MemoryUtil
# @run shell/timeout=600 LowMemoryTest2.sh
#

if [ ! -z "${TESTJAVA}" ] ; then
     JAVA=${TESTJAVA}/bin/java
     CLASSPATH=${TESTCLASSES}
     export CLASSPATH
else
     echo "--Error: TESTJAVA must be defined as the pathname of a jdk to test."
     exit 1
fi

# Test execution

failures=0

go() {
    echo ''
    sh -xc "$JAVA $TESTVMOPTS $*" 2>&1
    if [ $? != 0 ]; then failures=`expr $failures + 1`; fi
}

# Run test with each GC configuration
# 
# Notes: To ensure that perm gen fills up we disable class unloading.
# Also we set the max perm space to 8MB - otherwise the test takes too
# long to run. 

go -noclassgc -XX:PermSize=8m -XX:MaxPermSize=8m -XX:+UseSerialGC LowMemoryTest2
go -noclassgc -XX:PermSize=8m -XX:MaxPermSize=8m -XX:+UseParallelGC LowMemoryTest2
go -noclassgc -XX:PermSize=8m -XX:MaxPermSize=8m -XX:+UseParNewGC -XX:+UseConcMarkSweepGC \
    LowMemoryTest2

echo ''
if [ $failures -gt 0 ];
  then echo "$failures test(s) failed";
  else echo "All test(s) passed"; fi
exit $failures


