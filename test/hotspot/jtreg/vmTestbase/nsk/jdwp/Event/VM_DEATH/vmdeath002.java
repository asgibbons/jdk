/*
 * Copyright (c) 2001, 2024, Oracle and/or its affiliates. All rights reserved.
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
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */

package nsk.jdwp.Event.VM_DEATH;

import java.io.*;

import nsk.share.*;
import nsk.share.jpda.*;
import nsk.share.jdwp.*;

/**
 * Test for JDWP event: VM_DEATH.
 *
 * See vmdeath002.README for description of test execution.
 *
 * This class represents debugger part of the test.
 * Test is executed by invoking method runIt().
 * JDWP event is tested in the method waitForTestedEvent().
 *
 * @see #runIt()
 * @see #waitForTestedEvent()
 */
public class vmdeath002 {

    // exit status constants
    static final int JCK_STATUS_BASE = 95;
    static final int PASSED = 0;
    static final int FAILED = 2;

    // VM capability constants
    static final int VM_CAPABILITY_NUMBER = JDWP.Capability.CAN_REQUEST_VMDEATH_EVENT;
    static final String VM_CAPABILITY_NAME = "canRequestVMDeathEvent";

    // package and classes names constants
    static final String PACKAGE_NAME = "nsk.jdwp.Event.VM_DEATH";
    static final String TEST_CLASS_NAME = PACKAGE_NAME + "." + "vmdeath002";
    static final String DEBUGEE_CLASS_NAME = TEST_CLASS_NAME + "a";

    // tested JDWP event constants
    static final byte TESTED_EVENT_KIND = JDWP.EventKind.VM_DEATH;
    static final byte TESTED_EVENT_SUSPEND_POLICY = JDWP.SuspendPolicy.ALL;

    // usual scaffold objects
    ArgumentHandler argumentHandler = null;
    Log log = null;
    Binder binder = null;
    Debugee debugee = null;
    Transport transport = null;
    int waitTime = 0;  // minutes
    long timeout = 0;  // milliseconds
    boolean dead = false;
    boolean resumed = false;
    boolean success = true;

    // obtained data
    int eventRequestID = 0;

    // -------------------------------------------------------------------

    /**
     * Start test from command line.
     */
    public static void main (String argv[]) {
        int result = run(argv, System.out);
        if (result != 0) {
            throw new RuntimeException("Test failed");
        }
    }

    /**
     * Start test from JCK-compilant environment.
     */
    public static int run(String argv[], PrintStream out) {
        return new vmdeath002().runIt(argv, out);
    }

    // -------------------------------------------------------------------

    /**
     * Perform test execution.
     */
    public int runIt(String argv[], PrintStream out) {

        // make log for debugger messages
        argumentHandler = new ArgumentHandler(argv);
        log = new Log(out, argumentHandler);
        waitTime = argumentHandler.getWaitTime();
        timeout = waitTime * 60 * 1000;

        // execute test and display results
        try {
            log.display("\n>>> Starting debugee \n");

            // launch debuggee
            binder = new Binder(argumentHandler, log);
            log.display("Launching debugee");
            debugee = binder.bindToDebugee(DEBUGEE_CLASS_NAME);
            transport = debugee.getTransport();
            log.display("  ... debugee launched");
            log.display("");

            // set timeout for debuggee responces
            log.display("Setting timeout for debuggee responces: " + waitTime + " minute(s)");
            transport.setReadTimeout(timeout);
            log.display("  ... timeout set");

            // wait for debuggee started
            log.display("Waiting for VM_INIT event");
            debugee.waitForVMInit();
            log.display("  ... VM_INIT event received");

            // query debugee for VM-dependent ID sizes
            log.display("Querying for IDSizes");
            debugee.queryForIDSizes();
            log.display("  ... size of VM-dependent types adjusted");

            // check for VM capability
            log.display("\n>>> Checking VM capability \n");
            log.display("Getting VM capability: " + VM_CAPABILITY_NAME);
            boolean capable = debugee.getNewCapability(VM_CAPABILITY_NUMBER, VM_CAPABILITY_NAME);
            log.display("  ... got VM capability: " + capable);

            // exit as PASSED if this capability is not supported
            if (!capable) {
                out.println("TEST PASSED: unsupported VM capability: "
                            + VM_CAPABILITY_NAME);
                return PASSED;
            }

            // test JDWP event
            log.display("\n>>> Testing JDWP event \n");
            log.display("Making request for METHOD_DEATH event");
            requestTestedEvent();
            log.display("  ... got requestID: " + eventRequestID);
            log.display("");

            // resume debuggee
            log.display("Resumindg debuggee");
            debugee.resume();
            log.display("  ... debuggee resumed");
            log.display("");

            // wait for tested VM_DEATH event
            log.display("Waiting for VM_DEATH event received");
            waitForTestedEvent();
            log.display("  ... event received");
            log.display("");

            if (!dead) {
                // clear tested request for VM_DEATH event
                log.display("\n>>> Clearing request for tested event \n");
                clearTestedRequest();

                // finish debuggee after testing
                log.display("\n>>> Finishing debuggee \n");

                // resume debuggee
                log.display("Resuming debuggee");
                debugee.resume();
                log.display("  ... debuggee resumed");

                // wait for debuggee exited
                log.display("Waiting for VM_DEATH event");
                debugee.waitForVMDeath();
                dead = true;
                log.display("  ... VM_DEATH event received");
            } else if (!resumed) {
                // resume debuggee
                log.display("Resuming debuggee");
                debugee.resume();
                log.display("  ... debuggee resumed");
            }

        } catch (Failure e) {
            log.complain("TEST FAILED: " + e.getMessage());
            success = false;
        } catch (Exception e) {
            e.printStackTrace(out);
            log.complain("Caught unexpected exception while running the test:\n\t" + e);
            success = false;
        } finally {
            // quit debugee
            log.display("\n>>> Finishing test \n");
            quitDebugee();
        }

        // check test results
        if (!success) {
            log.complain("TEST FAILED");
            return FAILED;
        }

        out.println("TEST PASSED");
        return PASSED;

    }

    /**
     * Make request for tested METHOD_ENTRY event.
     */
    void requestTestedEvent() {
        Failure failure = new Failure("Error occured while makind request for tested event");

        // create command packet and fill requred out data
        log.display("Create command packet: " + "EventRequest.Set");
        CommandPacket command = new CommandPacket(JDWP.Command.EventRequest.Set);
        log.display("    eventKind: " + TESTED_EVENT_KIND);
        command.addByte(TESTED_EVENT_KIND);
        log.display("    eventPolicy: " + TESTED_EVENT_SUSPEND_POLICY);
        command.addByte(TESTED_EVENT_SUSPEND_POLICY);
        log.display("    modifiers: " + 0);
        command.addInt(0);
        command.setLength();
        log.display("  ... command packet composed");
        log.display("");

        // send command packet to debugee
        try {
            log.display("Sending command packet:\n" + command);
            transport.write(command);
            log.display("  ... command packet sent");
        } catch (IOException e) {
            log.complain("Unable to send command packet:\n\t" + e);
            success = false;
            throw failure;
        }
        log.display("");

        ReplyPacket reply = new ReplyPacket();

        // receive reply packet from debugee
        try {
            log.display("Waiting for reply packet");
            transport.read(reply);
            log.display("  ... packet received:\n" + reply);
        } catch (IOException e) {
            log.complain("Unable to read reply packet:\n\t" + e);
            success = false;
            throw failure;
        }
        log.display("");

        // check reply packet header
        try{
            log.display("Checking header of reply packet");
            reply.checkHeader(command.getPacketID());
            log.display("  .. packet header is correct");
        } catch (BoundException e) {
            log.complain("Bad header of reply packet:\n\t" + e.getMessage());
            success = false;
            throw failure;
        }

        // start parsing reply packet data
        log.display("Parsing reply packet:");
        reply.resetPosition();

        // extract requestID
        int requestID = 0;
        try {
            requestID = reply.getInt();
            log.display("    requestID: " + requestID);
        } catch (BoundException e) {
            log.complain("Unable to extract requestID from request reply packet:\n\t"
                        + e.getMessage());
            success = false;
            throw failure;
        }

        // check requestID
        if (requestID == 0) {
            log.complain("Unexpected null requestID returned: " + requestID);
            success = false;
            throw failure;
        }

        eventRequestID = requestID;

        // check for extra data in reply packet
        if (!reply.isParsed()) {
            log.complain("Extra trailing bytes found in request reply packet at: "
                        + reply.offsetString());
            success = false;
        }

        log.display("  ... reply packet parsed");

    }

    /**
     * Clear request for tested METHOD_ENTRY event.
     */
    void clearTestedRequest() {
        Failure failure = new Failure("Error occured while clearing request for tested event");

        // create command packet and fill requred out data
        log.display("Create command packet: " + "EventRequest.Clear");
        CommandPacket command = new CommandPacket(JDWP.Command.EventRequest.Clear);
        log.display("    event: " + TESTED_EVENT_KIND);
        command.addByte(TESTED_EVENT_KIND);
        log.display("    requestID: " + eventRequestID);
        command.addInt(eventRequestID);
        log.display("  ... command packet composed");
        log.display("");

        // send command packet to debugee
        try {
            log.display("Sending command packet:\n" + command);
            transport.write(command);
            log.display("  ... command packet sent");
        } catch (IOException e) {
            log.complain("Unable to send command packet:\n\t" + e);
            success = false;
            throw failure;
        }
        log.display("");

        ReplyPacket reply = new ReplyPacket();

        // receive reply packet from debugee
        try {
            log.display("Waiting for reply packet");
            transport.read(reply);
            log.display("  ... packet received:\n" + reply);
        } catch (IOException e) {
            log.complain("Unable to read reply packet:\n\t" + e);
            success = false;
            throw failure;
        }

        // check reply packet header
        try{
            log.display("Checking header of reply packet");
            reply.checkHeader(command.getPacketID());
            log.display("  .. packet header is correct");
        } catch (BoundException e) {
            log.complain("Bad header of reply packet:\n\t" + e.getMessage());
            success = false;
            throw failure;
        }

        // start parsing reply packet data
        log.display("Parsing reply packet:");
        reply.resetPosition();

        log.display("    no data");

        // check for extra data in reply packet
        if (!reply.isParsed()) {
            log.complain("Extra trailing bytes found in request reply packet at: "
                        + reply.offsetString());
            success = false;
        }

        log.display("  ... reply packet parsed");
    }

    /**
     * Wait for tested VM_DEATH event.
     */
    void waitForTestedEvent() {

        EventPacket eventPacket = null;

        // receive reply packet from debugee
        try {
            log.display("Waiting for event packet");
            eventPacket = debugee.getEventPacket(timeout);
            log.display("  ... event packet received:\n" + eventPacket);
        } catch (IOException e) {
            log.complain("Unable to read tested event packet:\n\t" + e);
            success = false;
            return;
        }

        // check reply packet header
        try{
            log.display("Checking header of event packet");
            eventPacket.checkHeader();
            log.display("  ... packet header is OK");
        } catch (BoundException e) {
            log.complain("Bad header of tested event packet:\n\t"
                        + e.getMessage());
            success = false;
            return;
        }

        // start parsing reply packet data
        log.display("Parsing event packet:");
        eventPacket.resetPosition();

        // get suspendPolicy value
        byte suspendPolicy = 0;
        try {
            suspendPolicy = eventPacket.getByte();
            log.display("    suspendPolicy: " + suspendPolicy);
        } catch (BoundException e) {
            log.complain("Unable to get suspendPolicy value from tested event packet:\n\t"
                        + e.getMessage());
            success = false;
            return;
        }

        // check suspendPolicy value
        if (suspendPolicy != TESTED_EVENT_SUSPEND_POLICY) {
            log.complain("Unexpected SuspendPolicy in tested event packet: " +
                        suspendPolicy + " (expected: " + TESTED_EVENT_SUSPEND_POLICY + ")");
            success = false;
        }

        // get events count
        int events = 0;
        try {
            events = eventPacket.getInt();
            log.display("    events: " + events);
        } catch (BoundException e) {
            log.complain("Unable to get events count from tested event packet:\n\t"
                        + e.getMessage());
            success = false;
            return;
        }

        // check events count
        if (events < 0) {
            log.complain("Negative value of events number in tested event packet: " +
                        events + " (expected: " + 1 + ")");
            success = false;
        }

        // extract each event
        int eventReceived = 0;
        for (int i = 0; i < events; i++) {
            log.display("    event #" + i + ":");

            // get eventKind
            byte eventKind = 0;
            try {
                eventKind = eventPacket.getByte();
                log.display("      eventKind: " + eventKind);
            } catch (BoundException e) {
                log.complain("Unable to get eventKind of event #" + i + " from tested event packet:\n\t"
                            + e.getMessage());
                success = false;
                return;
            }

            // check eventKind
            if (eventKind != TESTED_EVENT_KIND) {
                log.complain("Unexpected eventKind of event " + i + " in tested event packet: " +
                            eventKind + " (expected: " + TESTED_EVENT_KIND + ")");
                success = false;
                continue;
            }

            // get requestID
            int requestID = 0;
            try {
                requestID = eventPacket.getInt();
                log.display("      requestID: " + requestID);
            } catch (BoundException e) {
                log.complain("Unable to get requestID of event #" + i + " from tested event packet:\n\t"
                            + e.getMessage());
                success = false;
                dead = true;
                resumed = (suspendPolicy != TESTED_EVENT_SUSPEND_POLICY);
                return;
            }

            // check requestID
            if (requestID == eventRequestID) {
                eventReceived++;
            } else if (requestID == 0) {
                dead = true;
                resumed = (suspendPolicy != TESTED_EVENT_SUSPEND_POLICY);
            } else {
                log.complain("Unexpected requestID of event " + i + " in tested event packet: " +
                            requestID + " (expected: " + eventRequestID + " or 0)");
                success = false;
                return;
            }

        }

        // check for extra data in event packet
        if (!eventPacket.isParsed()) {
            int extra = eventPacket.length() - eventPacket.currentPosition();
            log.complain("Extra trailing bytes found in event packet at: "
                        + eventPacket.offsetString() + " (" + extra + " bytes)");
            success = false;
        }

        log.display("  ... event packet parsed");

        if (eventReceived <= 0) {
            log.complain("No requested event received: " + eventReceived + " (expected: 1)");
            success = false;
        }

    }

    /**
     * Disconnect debuggee and wait for it exited.
     */
    void quitDebugee() {
        if (debugee == null)
            return;

        // disconnect debugee
        if (!dead) {
            try {
                log.display("Disconnecting debuggee");
                debugee.dispose();
                log.display("  ... debuggee disconnected");
            } catch (Failure e) {
                log.display("Failed to finally disconnect debuggee:\n\t"
                            + e.getMessage());
            }
        }

        // wait for debugee exited
        log.display("Waiting for debuggee exit");
        int code = debugee.waitFor();
        log.display("  ... debuggee exited with exit code: " + code);

        // analize debugee exit status code
        if (code != JCK_STATUS_BASE + PASSED) {
            log.complain("Debuggee FAILED with exit code: " + code);
            success = false;
        }
    }

}
