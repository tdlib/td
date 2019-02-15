import MyWorker from './worker.js';
import './third_party/broadcastchannel.js';
import uuid4 from 'uuid/v4';
import log from './logger.js';

const sleep = ms => new Promise(res => setTimeout(res, ms));

/**
 * TDLib in browser
 *
 * TDLib can be used from javascript through the [JSON](https://github.com/tdlib/td#using-json) interface.
 * This is a convenient wrapper around it.
 * Internally it uses TDLib built with emscripten as asm.js or WebAssembly. All work happens in a WebWorker.
 * TdClient itself just sends queries to WebWorker, receive updates and results from WebWorker.
 *
 * <br><br>
 * Differences from TDLib API<br>
 * 1. updateFatalError error:string = Update; <br>
 * 3. file <..as in td_api..> idb_key:string = File; <br>
 * 2. setJsLogVerbosityLevel new_verbosity_level:string = Ok;
 * 3. inputFileBlob blob:<javascript blob> = InputFile;<br>
 * 4. readFilePart path:string offset:int64 size:int64 = FilePart;<br>
 *    filePart data:blob = FilePart;<br>
 * <br>
 */
class TdClient {
  /**
   * @callback updateCallback
   * @param {Object} update
   */

  /**
   * Create TdClient
   * @param {Object} options - Options
   * @param {updateCallback} options.onUpdate - Callback for all updates. Could also be set explicitly right after TdClient construction.
   * @param {number} [options.jsLogVerbosityLevel='info'] - Verbosity level for javascript part of the code (error, warning, info, log, debug)
   * @param {number} [options.logVerbosityLevel=2] - Verbosity level for tdlib
   * @param {string} [options.prefix=tdlib] Currently only one instance of TdClient per a prefix is allowed. All but one created instances will be automatically closed. Usually, the newest instance is kept alive.
   * @param {boolean} [options.isBackground=false] - When choosing which instance to keep alive, we prefer instance with isBackground=false
   * @param {string} [options.mode=wasm] - Type of tdlib build to use. 'asmjs' for asm.js and 'wasm' for WebAssembly.
   * @param {boolean} [options.readOnly=false] - Open tdlib in read-only mode. Changes to tdlib database won't be persisted. For debug only.
   */
  constructor(options) {
    log.setVerbosity(options.jsLogVerbosityLevel);
    this.worker = new MyWorker();
    var self = this;
    this.worker.onmessage = function(e) {
      let response = e.data;
      log.debug(
        'receive from worker: ',
        JSON.parse(
          JSON.stringify(response, (key, value) => {
            if (key === 'arr') {
              return undefined;
            }
            return value;
          })
        )
      );
      if ('@extra' in response) {
        var query_id = response['@extra'].query_id;
        var [resolve, reject] = self.query_callbacks.get(query_id);
        self.query_callbacks.delete(query_id);
        if ('@old_extra' in response['@extra']) {
          response['@extra'] = response['@extra']['@old_extra'];
        }
        if (resolve) {
          if (response['@type'] === 'error') {
            reject(response);
          } else {
            resolve(response);
          }
        }
      } else {
        if (response['@type'] === 'inited') {
          self.onInited();
          return;
        }
        if (
          response['@type'] === 'updateAuthorizationState' &&
          response.authorization_state['@type'] === 'authorizationStateClosed'
        ) {
          self.onClosed();
        }
        self.onUpdate(response);
      }
    };
    this.query_id = 0;
    this.query_callbacks = new Map();
    if ('onUpdate' in options) {
      this.onUpdate = options.onUpdate;
      delete options.onUpdate;
    }
    this.worker.postMessage({ '@type': 'init', options: options });
    this.closeOtherClients(options);
  }

  /**
   * Send query to tdlib.
   *
   * If query contains an '@extra' field, the same field will be added into the result.
   * '@extra' may contain any js object, it won't be sent to web worker.
   *
   * @param {Object} query - Query for tdlib.
   * @returns {Promise} Promise represents the result of the query.
   */
  send(query) {
    this.query_id++;
    if (query['@extra']) {
      query['@extra'] = {
        '@old_extra': JSON.parse(JSON.stringify(query.extra)),
        query_id: this.query_id
      };
    } else {
      query['@extra'] = {
        query_id: this.query_id
      };
    }
    if (query['@type'] === 'setJsLogVerbosityLevel') {
      log.setVerbosity(query.new_verbosity_level);
    }

    log.debug('send to worker: ', query);
    this.worker.postMessage(query);
    return new Promise((resolve, reject) => {
      this.query_callbacks.set(this.query_id, [resolve, reject]);
    });
  }

  /** @private */
  onBroadcastMessage(e) {
    var message = e.data;
    log.info('got broadcast message: ', message);
    if (message.isBackground && !this.isBackground) {
      // continue
    } else if (
      (!message.isBackground && this.isBackground) ||
      message.timestamp > this.timestamp
    ) {
      this.close();
      return;
    }
    if (message.state === 'closed') {
      this.waitSet.delete(message.uid);
      if (this.waitSet.size === 0) {
        log.info('onWaitSetEmpty');
        this.onWaitSetEmpty();
        this.onWaitSetEmpty = () => {};
      }
    } else {
      this.waitSet.add(message.uid);
      if (message.state !== 'closing') {
        this.postState();
      }
    }
  }

  /** @private */
  postState() {
    let state = {
      id: this.uid,
      state: this.state,
      timestamp: this.timestamp,
      isBackground: this.isBackground
    };
    log.info('Post state: ', state);
    this.channel.postMessage(state);
  }

  /** @private */
  onWaitSetEmpty() {
    // nop
  }

  /** @private */
  onInited() {
    this.isInited = true;
    this.doSendStart();
  }

  /** @private */
  sendStart() {
    this.wantSendStart = true;
    this.doSendStart();
  }

  /** @private */
  doSendStart() {
    if (!this.isInited || !this.wantSendStart || this.state !== 'start') {
      return;
    }
    this.wantSendStart = false;
    this.state = 'active';
    let query = { '@type': 'start' };
    log.info('send to worker: ', query);
    this.worker.postMessage(query);
  }

  /** @private */
  onClosed() {
    this.isClosing = true;
    this.worker.terminate();
    log.info('worker is terminated');
    this.state = 'closed';
    this.postState();
  }

  /** @private */
  close() {
    if (this.isClosing) {
      return;
    }
    this.isClosing = true;

    log.info('close state: ', this.state);

    if (this.state === 'start') {
      this.onClosed();
      this.onUpdate({
        '@type': 'updateAuthorizationState',
        authorization_state: {
          '@type': 'authorizationStateClosed'
        }
      });
      return;
    }

    let query = { '@type': 'close' };
    log.info('send to worker: ', query);
    this.worker.postMessage(query);

    this.state = 'closing';
    this.postState();
  }

  /** @private */
  async closeOtherClients(options) {
    this.uid = uuid4();
    this.state = 'start';
    this.isBackground = !!options.isBackground;
    this.timestamp = Date.now();
    this.waitSet = new Set();

    log.info('close other clients');
    let prefix = options.prefix || 'tdlib';
    this.channel = new BroadcastChannel(prefix);

    this.postState();

    var self = this;
    this.channel.onmessage = message => {
      self.onBroadcastMessage(message);
    };

    await sleep(300);
    if (this.waitSet.size !== 0) {
      await new Promise(resolve => {
        self.onWaitSetEmpty = resolve;
      });
    }
    this.sendStart();
  }

  /** @private */
  onUpdate(response) {
    log.info('ignore onUpdate');
    //nop
  }
}
export default TdClient;
