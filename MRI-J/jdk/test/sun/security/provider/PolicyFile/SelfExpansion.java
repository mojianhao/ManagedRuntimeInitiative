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
 * @author  Ram Marti
 * @bug 4350951
 *
 * @summary generalized "self" expansion in JAAS policy files
 *
 * @run main/othervm/policy=SelfExpansion.policy SelfExpansion
 */

import java.security.*;
import javax.security.auth.Subject;

public class SelfExpansion {
    public static void main(String[] args) throws Exception {
        Subject s = new Subject();
        s.getPrincipals().add
                (new javax.security.auth.x500.X500Principal("CN=test"));
        s.getPrivateCredentials().add(new String("test"));
        try {
            Subject.doAsPrivileged(s, new PrivilegedAction() {
                public Object run() {
                    java.util.Iterator i = Subject.getSubject
                                (AccessController.getContext
                                ()).getPrivateCredentials().iterator();
                    return i.next();
                }
            }, null);
            System.out.println("Test succeeded");
        } catch (Exception e) {
            System.out.println("Test failed");
            e.printStackTrace();
            throw e;
        }
    }
}
