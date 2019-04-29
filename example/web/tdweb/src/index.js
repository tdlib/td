import MyWorker from './worker.js';
import localforage from 'localforage';
import './third_party/broadcastchannel.js';
import uuid4 from 'uuid/v4';
import log from './logger.js';

const sleep = ms => new Promise(res => setTimeout(res, ms));

/**
 * TDLib in a browser
 *
 * TDLib can be compiled to WebAssembly or asm.js using Emscripten compiler and used in a browser from JavaScript.
 * This is a convenient wrapper for TDLib in a browser which controls TDLib instance creation, handles interaction
 * with TDLib and manages a filesystem for persistent TDLib data.
 * TDLib instance is created in a Web Worker to run it in a separate thread.
 * TdClient just sends queries to the Web Worker and receives updates and results from it.
 * <br>
 * <br>
 * Differences from the TDLib JSON API:<br>
 * 1. Added the update <code>updateFatalError error:string = Update;</code> which is sent whenever TDLib encounters a fatal error.<br>
 * 2. Added the method <code>setJsLogVerbosityLevel new_verbosity_level:string = Ok;</code>, which allows to change the verbosity level of tdweb logging.<br>
 * 3. Added the possibility to use blobs as input files via the constructor <code>inputFileBlob data:<JavaScript blob> = InputFile;</code>.<br>
 * 4. The class <code>filePart</code> contains data as a JavaScript blob instead of a base64-encoded string.<br>
 * 5. The methods <code>getStorageStatistics</code>, <code>getStorageStatisticsFast</code>, <code>optimizeStorage</code>, <code>addProxy</code> are not supported.<br>
 * 6. Added the field <code>idb_key</code> to <code>file</code> object, which contains the IndexedDB key in which the file content is stored.<br>
 *    This field is non-empty only for fully downloaded files. IndexedDB database name is chosen during TdClient creation via options.instanceName parameter.<br>
 * <br>
 */
class TdClient {
  /**
   * @callback TdClient~updateCallback
   * @param {Object} update The update.
   */

  /**
   * Create TdClient.
   * @param {Object} options - Options for TDLib instance creation.
   * @param {TdClient~updateCallback} options.onUpdate - Callback for all incoming updates.
   * @param {string} [options.instanceName=tdlib] - Name of the TDLib instance. Currently only one instance of TdClient with a given name is allowed. All but one instances with the same name will be automatically closed. Usually, the newest non-background instance is kept alive. Files will be stored in an IndexedDb table with the same name.
   * @param {boolean} [options.isBackground=false] - Pass true, if the instance is opened from the background.
   * @param {string} [options.jsLogVerbosityLevel=info] - The initial verbosity level of the JavaScript part of the code (one of 'error', 'warning', 'info', 'log', 'debug').
   * @param {number} [options.logVerbosityLevel=2] - The initial verbosity level for the TDLib internal logging (0-1023).
   * @param {boolean} [options.useDatabase=true] - Pass false to use TDLib without database and secret chats. It will significantly improve loading time, but some functionality will be unavailable.
   * @param {boolean} [options.readOnly=false] - For debug only. Pass true to open TDLib database in read-only mode
   * @param {string} [options.mode=auto] - For debug only. The type of the TDLib build to use. 'asmjs' for asm.js and 'wasm' for WebAssembly. If mode == 'auto' WebAbassembly will be used if supported by browser, asm.js otherwise.
   */
  constructor(options) {
    log.setVerbosity(options.jsLogVerbosityLevel);
    this.worker = new MyWorker();
    var self = this;
    this.worker.onmessage = e => {
      self.onResponse(e.data);
    };
    this.query_id = 0;
    this.query_callbacks = new Map();
    if ('onUpdate' in options) {
      this.onUpdate = options.onUpdate;
      delete options.onUpdate;
    }
    options.instanceName = options.instanceName || 'tdlib';
    this.fileManager = new FileManager(options.instanceName);
    this.worker.postMessage({ '@type': 'init', options: options });
    this.closeOtherClients(options);
  }

  /**
   * Send a query to TDLib.
   *
   * If the query contains the field '@extra', the same field will be added into the result.
   *
   * @param {Object} query - The query for TDLib. See the [td_api.tl]{@link https://github.com/tdlib/td/blob/master/td/generate/scheme/td_api.tl} scheme or
   *                         the automatically generated [HTML documentation]{@link https://core.telegram.org/tdlib/docs/td__api_8h.html}
   *                         for a list of all available TDLib [methods]{@link https://core.telegram.org/tdlib/docs/classtd_1_1td__api_1_1_function.html} and
   *                         [classes]{@link https://core.telegram.org/tdlib/docs/classtd_1_1td__api_1_1_object.html}.
   * @returns {Promise} Promise object represents the result of the query.
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
    let res = new Promise((resolve, reject) => {
      this.query_callbacks.set(this.query_id, [resolve, reject]);
    });
    this.externalPostMessage(query);
    return res;
  }

  /** @private */
  externalPostMessage(query) {
    let unsupportedMethods = [
      'getStorageStatistics',
      'getStorageStatisticsFast',
      'optimizeStorage',
      'addProxy',
      'init',
      'start'
    ];
    if (unsupportedMethods.includes(query['@type'])) {
      this.onResponse({
        '@type': 'error',
        '@extra': query['@extra'],
        code: 400,
        message: "Method '" + query['@type'] + "' is not supported"
      });
      return;
    }
    if (query['@type'] === 'readFile') {
      this.readFile(query);
      return;
    }
    this.worker.postMessage(query);
  }

  /** @private */
  async readFile(query) {
    let response = await this.fileManager.readFile(query);
    this.onResponse(response);
  }

  /** @private */
  onResponse(response) {
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

    // for FileManager
    response = this.prepareResponse(response);

    if ('@extra' in response) {
      var query_id = response['@extra'].query_id;
      var [resolve, reject] = this.query_callbacks.get(query_id);
      this.query_callbacks.delete(query_id);
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
        this.onInited();
        return;
      }
      if (response['@type'] === 'fsInited') {
        this.onFsInited();
        return;
      }
      if (
        response['@type'] === 'updateAuthorizationState' &&
        response.authorization_state['@type'] === 'authorizationStateClosed'
      ) {
        this.onClosed();
      }
      this.onUpdate(response);
    }
  }

  /** @private */
  prepareFile(file) {
    return this.fileManager.registerFile(file);
  }

  /** @private */
  prepareResponse(response) {
    if (response['@type'] === 'file') {
      return this.prepareFile(response);
    }
    for (var key in response) {
      let field = response[key];
      if (field && typeof field === 'object') {
        response[key] = this.prepareResponse(field);
      }
    }
    return response;
  }

  /** @private */
  onBroadcastMessage(e) {
    var message = e.data;
    if (message.uid === this.uid) {
      log.info('ignore self broadcast message: ', message);
      return;
    }
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
  onFsInited() {
    this.fileManager.init();
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
    this.channel = new BroadcastChannel(options.instanceName);

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
  onUpdate(update) {
    log.info('ignore onUpdate');
    //nop
  }
}

/** @private */
class FileManager {
  constructor(instanceName) {
    this.instanceName = instanceName;
    this.cache = new Map();
    this.pending = [];
    this.transaction_id = 0;
  }

  init() {
    let self = this;
    this.idb = new Promise((resolve, reject) => {
      const request = window.indexedDB.open(self.instanceName);
      request.onsuccess = () => resolve(request.result);
      request.onerror = () => reject(request.error);
    });
    //this.store = localforage.createInstance({
    //name: instanceName
    //});
    this.isInited = true;
  }

  registerFile(file) {
    if (file.idb_key || file.arr) {
      file.is_downloading_completed = true;
      var info = {};
      let cached_info = this.cache.get(file.id);
      if (cached_info !== undefined) {
        info = cached_info;
      } else {
        this.cache.set(file.id, info);
      }
      if (file.idb_key) {
        info.idb_key = file.idb_key;
      }
      if (file.arr) {
        info.arr = file.arr;
      }
    }
    return file;
  }

  async flushLoad() {
    let pending = this.pending;
    this.pending = [];
    let idb = await this.idb;
    let transaction_id = this.transaction_id++;
    let read = idb
      .transaction(['keyvaluepairs'], 'readonly')
      .objectStore('keyvaluepairs');
    log.debug('Load group of files from idb', pending.length);
    for (const query of pending) {
      const request = read.get(query.key);
      request.onsuccess = event => {
        const blob = event.target.result;
        if (blob) {
          query.resolve({ data: blob, transaction_id: transaction_id });
        } else {
          query.reject();
        }
      };
      request.onerror = query.reject;
    }
  }

  load(key, resolve, reject) {
    if (this.pending.length === 0) {
      let self = this;
      setTimeout(() => {
        self.flushLoad();
      }, 1);
    }
    this.pending.push({ key: key, resolve: resolve, reject: reject });
  }

  async doLoad(info) {
    if (info.arr) {
      return { data: new Blob([info.arr]), transaction_id: -1 };
    }
    let idb_key = info.idb_key;
    let self = this;
    //return this.store.getItem(idb_key);
    return await new Promise((resolve, reject) => {
      self.load(idb_key, resolve, reject);
    });
  }

  async readFile(query) {
    try {
      if (!this.isInited) {
        throw new Error('FileManager is not inited');
      }
      let info = this.cache.get(query.file_id);
      if (!info) {
        throw new Error('File is not loaded');
      }
      let response = await this.doLoad(info);
      return {
        '@type': 'blob',
        '@extra': query['@extra'],
        data: response.data,
        transaction_id: response.transaction_id
      };
    } catch (e) {
      return {
        '@type': 'error',
        '@extra': query['@extra'],
        code: 400,
        message: e
      };
    }
  }
}

export default TdClient;
