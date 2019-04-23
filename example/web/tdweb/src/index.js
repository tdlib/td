import MyWorker from './worker.js';
import './third_party/broadcastchannel.js';
import uuid4 from 'uuid/v4';
import log from './logger.js';

const sleep = ms => new Promise(res => setTimeout(res, ms));

/**
 * TDLib in a browser
 *
 * TDLib can be compiled to WebAssembly or asm.js using Emscripten compiler and used in a browser from JavaScript.
 * This is a convenient wrapper for TDLib in a browser which controls TDLib instance creation, handles interaction
 * with the TDLib and manages a filesystem for persistent TDLib data.
 * TDLib instance is created in a Web Worker, because TDLib needs synchronous access to filesystem and the IndexedDB.
 * TdClient just sends queries to the Web Worker and receive updates and results from it.
 * <br>
 * <br>
 * Differences from TDLib API:<br>
 * 1. Added the update <code>updateFatalError error:string = Update;</code> which is sent whenever a TDLib fatal error is encountered.<br>
 * 2. Added the field <code>idb_key</code> to <code>file</code> object, which contains IndexedDB key in which the file content is stored.<br>
 *    This field is non-empty only for fully downloaded files. IndexedDB database name is chosen during TdClient creation.<br>
 * 3. Added the method <code>setJsLogVerbosityLevel new_verbosity_level:string = Ok;</code>, which allows to change the verbosity level of tdweb logging.<br>
 * 4. Added the possibility to use blobs as input files via constructor <code>inputFileBlob blob:<JavaScript blob> = InputFile;</code>.<br>
 * 5. Added the method <code>readFilePart path:string offset:int64 size:int64 = FilePart;</code> and class <code>filePart data:<JavaScript blob> = FilePart;</code><br>
 *    which can be used on a partially downloaded file to support media streaming.<br>
 * 6. Methods <code>getStorageStatistics</code>, <code>getStorageStatisticsFast</code>, <code>optimizeStorage</code>, <code>addProxy</code> are not supported.<br>
 * <br>
 */
class TdClient {
  /**
   * @callback TdClient~updateCallback
   * @param {Object} The update.
   */

  /**
   * Create TdClient.
   * @param {Object} options - The options for TDLib instance creation.
   * @param {TdClient~updateCallback} options.onUpdate - The callback for all incoming updates.
   * @param {string} [options.prefix=tdlib] - The name of the IndexedDB database which will be used for persistent data storage. Currently only one instance of TdClient per a database is allowed. All but one created instances will be automatically closed. Usually, the newest non-background instance is kept alive.
   * @param {boolean} [options.isBackground=false] - Pass true, if the instance is opened from the background.
   * @param {string} [options.mode=wasm] - The type of the TDLib build to use. 'asmjs' for asm.js and 'wasm' for WebAssembly.
   * @param {string} [options.jsLogVerbosityLevel='info'] - The initial verbosity level of the JavaScript part of the code (one of 'error', 'warning', 'info', 'log', 'debug').
   * @param {number} [options.logVerbosityLevel=2] - The initial verbosity level for TDLib internal logging (0-1023).
   * @param {boolean} [options.noDb=false] - Pass true to use TDLib without database and secret chats. It will significantly improve load time, but some functionality will be unavailable.
   * @param {boolean} [options.readOnly=false] - Pass true to open TDLib database in read-only mode. For debug only.
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
   * Send a query to TDLib.
   *
   * If the query contains an '@extra' field, the same field will be added into the result.
   *
   * @param {Object} query - The query for TDLib. See the [td_api.tl]{@link https://github.com/tdlib/td/blob/master/td/generate/scheme/td_api.tl} scheme or
   *                         the automatically generated [HTML documentation]{@link https://core.telegram.org/tdlib/docs/td__api_8h.html}
   *                         for a list of all available TDLib [methods]{@link https://core.telegram.org/tdlib/docs/classtd_1_1td__api_1_1_function.html} and
   *                         [classes]{@link https://core.telegram.org/tdlib/docs/classtd_1_1td__api_1_1_object.html}.
   * @returns {Promise} Promise object represents the result of the query.
   */
  send(query) {
    let unsupportedMethods = ['getStorageStatistics', 'getStorageStatisticsFast', 'optimizeStorage', 'addProxy'];
    if (unsupportedMethods.includes(query['@type'])) {
      return;  // TODO what we need to return?
    }

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
