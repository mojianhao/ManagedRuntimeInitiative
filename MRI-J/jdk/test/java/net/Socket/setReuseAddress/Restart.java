/*
 * Copyright 2001 Sun Microsystems, Inc.  All Rights Reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 */

/*
 * @test
 * @bug 4476378
 * @summary Check that SO_REUSEADDR allows a server to restart
 *          after a crash.
 */
import java.net.*;

public class Restart {

    /*
     * Test that a server can bind to the same port after
     * a crash -- ie: while previous connection still in
     * TIME_WAIT state we should be able to re-bind if
     * SO_REUSEADDR is enabled.
     */

    public static void main(String args[]) throws Exception {

        InetSocketAddress isa = new InetSocketAddress(0);
        ServerSocket ss = new ServerSocket();
        ss.bind(isa);

        int port = ss.getLocalPort();

        Socket s1 = new Socket(InetAddress.getLocalHost(), port);
        Socket s2 = ss.accept();

        // close server socket and the accepted connection
        ss.close();
        s2.close();

        boolean failed = false;

        ss = new ServerSocket();
        ss.bind( new InetSocketAddress(port) );
        ss.close();

        // close the client socket
        s1.close();
    }
}
