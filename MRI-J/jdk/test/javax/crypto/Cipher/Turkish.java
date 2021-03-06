/*
 * Copyright 2005-2007 Sun Microsystems, Inc.  All Rights Reserved.
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

/**
 * @test
 * @bug 6220064
 * @summary make sure everything works ok in the Turkish local (dotted/dotless i problem)
 * @author Andreas Sterbenz
 */

import java.util.Locale;

import javax.crypto.Cipher;

public class Turkish {

    public static void main(String[] args) throws Exception {
        Locale.setDefault(new Locale("tr", "TR"));

        System.out.println(Cipher.getInstance("RSA/ECB/PKCS1Padding"));
        System.out.println(Cipher.getInstance("RSA/ECB/PKCS1PADDING"));
        System.out.println(Cipher.getInstance("rsa/ecb/pkcs1padding"));
        System.out.println(Cipher.getInstance("Blowfish"));
        System.out.println(Cipher.getInstance("blowfish"));
        System.out.println(Cipher.getInstance("BLOWFISH"));

        System.out.println("OK");
    }
}
