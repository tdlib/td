//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
package org.drinkless.tdlib;

/**
 * Class used for managing internal TDLib logging.
 * Use TdApi.*Log* methods instead.
 */
public final class Log {
    /**
     * Changes TDLib log verbosity.
     *
     * @deprecated As of TDLib 1.4.0 in favor of {@link TdApi.SetLogVerbosityLevel}, to be removed in the future.
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
    @Deprecated
    public static native void setVerbosityLevel(int verbosityLevel);

    /**
     * Sets file path for writing TDLib internal log. By default TDLib writes logs to the System.err.
     * Use this method to write the log to a file instead.
     *
     * @deprecated As of TDLib 1.4.0 in favor of {@link TdApi.SetLogStream}, to be removed in the future.
     * @param filePath Path to a file for writing TDLib internal log. Use an empty path to
     *                 switch back to logging to the System.err.
     * @return whether opening the log file succeeded.
     */
    @Deprecated
    public static native boolean setFilePath(String filePath);

    /**
     * Changes the maximum size of TDLib log file.
     *
     * @deprecated As of TDLib 1.4.0 in favor of {@link TdApi.SetLogStream}, to be removed in the future.
     * @param maxFileSize The maximum size of the file to where the internal TDLib log is written
     *                    before the file will be auto-rotated. Must be positive. Defaults to 10 MB.
     */
    @Deprecated
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
