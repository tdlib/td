//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2020
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
package org.drinkless.tdlib;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReadWriteLock;
import java.util.concurrent.locks.ReentrantReadWriteLock;

/**
 * Main class for interaction with the TDLib.
 */
public final class Client implements Runnable {
    /**
     * Interface for handler for results of queries to TDLib and incoming updates from TDLib.
     */
    public interface ResultHandler {
        /**
         * Callback called on result of query to TDLib or incoming update from TDLib.
         *
         * @param object Result of query or update of type TdApi.Update about new events.
         */
        void onResult(TdApi.Object object);
    }

    /**
     * Interface for handler of exceptions thrown while invoking ResultHandler.
     * By default, all such exceptions are ignored.
     * All exceptions thrown from ExceptionHandler are ignored.
     */
    public interface ExceptionHandler {
        /**
         * Callback called on exceptions thrown while invoking ResultHandler.
         *
         * @param e Exception thrown by ResultHandler.
         */
        void onException(Throwable e);
    }

    /**
     * Sends a request to the TDLib.
     *
     * @param query            Object representing a query to the TDLib.
     * @param resultHandler    Result handler with onResult method which will be called with result
     *                         of the query or with TdApi.Error as parameter. If it is null, nothing
     *                         will be called.
     * @param exceptionHandler Exception handler with onException method which will be called on
     *                         exception thrown from resultHandler. If it is null, then
     *                         defaultExceptionHandler will be called.
     * @throws NullPointerException if query is null.
     */
    public void send(TdApi.Function query, ResultHandler resultHandler, ExceptionHandler exceptionHandler) {
        if (query == null) {
            throw new NullPointerException("query is null");
        }

        readLock.lock();
        try {
            if (isClientDestroyed) {
                if (resultHandler != null) {
                    handleResult(new TdApi.Error(500, "Client is closed"), resultHandler, exceptionHandler);
                }
                return;
            }

            long queryId = currentQueryId.incrementAndGet();
            handlers.put(queryId, new Handler(resultHandler, exceptionHandler));
            nativeClientSend(nativeClientId, queryId, query);
        } finally {
            readLock.unlock();
        }
    }

    /**
     * Sends a request to the TDLib with an empty ExceptionHandler.
     *
     * @param query         Object representing a query to the TDLib.
     * @param resultHandler Result handler with onResult method which will be called with result
     *                      of the query or with TdApi.Error as parameter. If it is null, then
     *                      defaultExceptionHandler will be called.
     * @throws NullPointerException if query is null.
     */
    public void send(TdApi.Function query, ResultHandler resultHandler) {
        send(query, resultHandler, null);
    }

    /**
     * Synchronously executes a TDLib request. Only a few marked accordingly requests can be executed synchronously.
     *
     * @param query Object representing a query to the TDLib.
     * @return request result.
     * @throws NullPointerException if query is null.
     */
    public static TdApi.Object execute(TdApi.Function query) {
        if (query == null) {
            throw new NullPointerException("query is null");
        }
        return nativeClientExecute(query);
    }

    /**
     * Replaces default exception handler to be invoked on exceptions thrown from updateHandler and all other ResultHandler.
     *
     * @param defaultExceptionHandler Default exception handler. If null Exceptions are ignored.
     */
    public void setDefaultExceptionHandler(Client.ExceptionHandler defaultExceptionHandler) {
        this.defaultExceptionHandler = defaultExceptionHandler;
    }

    /**
     * Overridden method from Runnable, do not call it directly.
     */
    @Override
    public void run() {
        while (!stopFlag) {
            receiveQueries(300.0 /*seconds*/);
        }
    }

    /**
     * Creates new Client.
     *
     * @param updateHandler           Handler for incoming updates.
     * @param updateExceptionHandler  Handler for exceptions thrown from updateHandler. If it is null, exceptions will be iggnored.
     * @param defaultExceptionHandler Default handler for exceptions thrown from all ResultHandler. If it is null, exceptions will be iggnored.
     * @return created Client
     */
    public static Client create(ResultHandler updateHandler, ExceptionHandler updateExceptionHandler, ExceptionHandler defaultExceptionHandler) {
        Client client = new Client(updateHandler, updateExceptionHandler, defaultExceptionHandler);
        new Thread(client, "TDLib thread").start();
        return client;
    }

    /**
     * Closes Client.
     */
    public void close() {
        writeLock.lock();
        try {
            if (isClientDestroyed) {
                return;
            }
            if (!stopFlag) {
                send(new TdApi.Close(), null);
            }
            isClientDestroyed = true;
            while (!stopFlag) {
                Thread.yield();
            }
            while (!handlers.isEmpty()) {
                receiveQueries(300.0);
            }
            updateHandlers.remove(nativeClientId);
            destroyNativeClient(nativeClientId);
        } finally {
            writeLock.unlock();
        }
    }

    private final ReadWriteLock readWriteLock = new ReentrantReadWriteLock();
    private final Lock readLock = readWriteLock.readLock();
    private final Lock writeLock = readWriteLock.writeLock();

    private volatile boolean stopFlag = false;
    private volatile boolean isClientDestroyed = false;
    private final long nativeClientId;

    private static final ConcurrentHashMap<Long, Handler> updateHandlers = new ConcurrentHashMap<Long, Handler>();

    private final ConcurrentHashMap<Long, Handler> handlers = new ConcurrentHashMap<Long, Handler>();
    private final AtomicLong currentQueryId = new AtomicLong();

    private volatile ExceptionHandler defaultExceptionHandler = null;

    private static final int MAX_EVENTS = 1000;
    private final long[] eventIds = new long[MAX_EVENTS];
    private final TdApi.Object[] events = new TdApi.Object[MAX_EVENTS];

    private static class Handler {
        final ResultHandler resultHandler;
        final ExceptionHandler exceptionHandler;

        Handler(ResultHandler resultHandler, ExceptionHandler exceptionHandler) {
            this.resultHandler = resultHandler;
            this.exceptionHandler = exceptionHandler;
        }
    }

    private Client(ResultHandler updateHandler, ExceptionHandler updateExceptionHandler, ExceptionHandler defaultExceptionHandler) {
        nativeClientId = createNativeClient();
        updateHandlers.put(nativeClientId, new Handler(updateHandler, updateExceptionHandler));
        this.defaultExceptionHandler = defaultExceptionHandler;
    }

    @Override
    protected void finalize() throws Throwable {
        try {
            close();
        } finally {
            super.finalize();
        }
    }

    private void processResult(long id, TdApi.Object object) {
        if (object instanceof TdApi.UpdateAuthorizationState) {
            if (((TdApi.UpdateAuthorizationState) object).authorizationState instanceof TdApi.AuthorizationStateClosed) {
                stopFlag = true;
            }
        }
        Handler handler;
        if (id == 0) {
            // update handler stays forever
            handler = updateHandlers.get(nativeClientId);
        } else {
            handler = handlers.remove(id);
        }
        if (handler == null) {
            return;
        }

        handleResult(object, handler.resultHandler, handler.exceptionHandler);
    }

    private void handleResult(TdApi.Object object, ResultHandler resultHandler, ExceptionHandler exceptionHandler) {
        if (resultHandler == null) {
            return;
        }

        try {
            resultHandler.onResult(object);
        } catch (Throwable cause) {
            if (exceptionHandler == null) {
                exceptionHandler = defaultExceptionHandler;
            }
            if (exceptionHandler != null) {
                try {
                    exceptionHandler.onException(cause);
                } catch (Throwable ignored) {
                }
            }
        }
    }

    private void receiveQueries(double timeout) {
        int resultN = nativeClientReceive(nativeClientId, eventIds, events, timeout);
        for (int i = 0; i < resultN; i++) {
            processResult(eventIds[i], events[i]);
            events[i] = null;
        }
    }

    private static native long createNativeClient();

    private static native void nativeClientSend(long nativeClientId, long eventId, TdApi.Function function);

    private static native int nativeClientReceive(long nativeClientId, long[] eventIds, TdApi.Object[] events, double timeout);

    private static native TdApi.Object nativeClientExecute(TdApi.Function function);

    private static native void destroyNativeClient(long nativeClientId);
}
