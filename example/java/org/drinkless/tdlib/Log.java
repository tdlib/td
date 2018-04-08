//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2018
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
package org.drinkless.tdlib;

/**
 * Class for managing internal TDLib logging.
 */
public final class Log {
    /**
     * Changes TDLib log verbosity.
     *
     * @param verbosityLevel New value of log verbosity level. Must be non-negative.
     *                       Value 0 corresponds to fatal errors,
     *                       value 1 corresponds to java.util.logging.Level.SEVERE,
     *                       value 2 corresponds to java.util.logging.Level.WARNING,
     *                       value 3 corresponds to java.util.logging.Level.INFO,
     *                       value 4 corresponds to java.util.logging.Level.FINE,
     *                       value 5 corresponds to java.util.logging.Level.FINER,
     *                       value greater than 5 can be used to enable even more logging.
     *                       Default value of the log verbosity level is 5.
     */
    public static native void setVerbosityLevel(int verbosityLevel);

    /**
     * Sets file path for writing TDLib internal log. By default TDLib writes logs to the System.err.
     * Use this method to write the log to a file instead.
     *
     * @param filePath Path to a file for writing TDLib internal log. Use an empty path to
     *                 switch back to logging to the System.err.
     * @return whether opening the log file succeeded.
     */
    public static native boolean setFilePath(String filePath);

    /**
     * Changes maximum size of TDLib log file.
     *
     * @param maxFileSize Maximum size of the file to where the internal TDLib log is written
     *                    before the file will be auto-rotated. Must be positive. Defaults to 10 MB.
     */
    public static native void setMaxFileSize(long maxFileSize);

    /**
     * This function is called from the JNI when a fatal error happens to provide a better error message.
     * The function does not return.
     *
     * @param errorMessage Error message.
     */
    private static void onFatalError(String errorMessage) {
        class ThrowError implements Runnable {
            private ThrowError(String errorMessage) {
                this.errorMessage = errorMessage;
            }

            @Override
            public void run() {
                throw new RuntimeException("TDLib fatal error: " + errorMessage);
            }

            private final String errorMessage;
        }

        new Thread(new ThrowError(errorMessage), "TDLib fatal error thread").start();
        while (true) {
            try {
                Thread.sleep(1000);     // milliseconds
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
            }
        }
    }
}
