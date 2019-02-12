import localforage from 'localforage';
import log from './logger.js';
import {
  instantiateAny
} from './wasm-utils.js';

import td_wasm_release from './prebuilt/release/td_wasm.wasm';

// Uncomment for asmjs support
//import td_asmjs_mem_release from './prebuilt/release/td_asmjs.js.mem';

import { detect } from 'detect-browser';
const browser = detect();
const tdlibVersion = 6;
const localForageDrivers = [localforage.INDEXEDDB, localforage.LOCALSTORAGE, 'memoryDriver'];

async function initLocalForage() {
  // Implement the driver here.
  var memoryDriver = {
    _driver: 'memoryDriver',
    _initStorage: function(options) {
      var dbInfo = {};
      if (options) {
        for (var i in options) {
          dbInfo[i] = options[i];
        }
      }
      this._dbInfo = dbInfo;
      this._map = new Map();
    },
    clear: async function() {
      this._map.clear();
    },
    getItem: async function(key) {
      let value = this._map.get(key);
      console.log("getItem", this._map, key, value);
      return value;
    },
    iterate: async function(iteratorCallback) {
      log.error("iterate is not supported");
    },
    key: async function(n) {
      log.error("key n is not supported");
    },
    keys: async function() {
      return this._map.keys();
    },
    length: async function() {
      return this._map.size();
    },
    removeItem: async function(key) {
      this._map.delete(key)
    },
    setItem: async function(key, value) {
      let originalValue = this._map.get(key);
      console.log("setItem", this._map, key, value);
      this._map.set(key, value);
      return originalValue;
    }
  }

  // Add the driver to localForage.
  localforage.defineDriver(memoryDriver);
}

async function loadTdLibWasm() {
  let Module = await import('./prebuilt/release/td_wasm.js');
  log.info('got td_wasm.js');
  let td_wasm = td_wasm_release;
  let TdModule = new Promise((resolve, reject) =>
    Module({
      onRuntimeInitialized: () => {
        log.info('runtime intialized');
      },
      instantiateWasm: (imports, successCallback) => {
        log.info('start instantiateWasm');
        let next = instance => {
          log.info('finish instantiateWasm');
          successCallback(instance);
        };
        instantiateAny(tdlibVersion, td_wasm, imports).then(next);
        return {};
      },
      ENVIROMENT: 'WORKER'
    }).then(m => {
      delete m.then;
      resolve(m);
    })
  );

  return TdModule;
}

// Uncomment for asmjs support
//async function loadTdLibAsmjs() {
  //let Module = await import('./prebuilt/release/td_asmjs.js');
  //console.log('got td_wasm.js');
  //let fromFile = 'td_asmjs.js.mem';
  //let toFile = td_asmjs_mem_release;
  //let TdModule = new Promise((resolve, reject) =>
    //Module({
      //onRuntimeInitialized: () => {
        //console.log('runtime intialized');
      //},
      //locateFile: name => {
        //if (name === fromFile) {
          //return toFile;
        //}
        //return name;
      //},
      //ENVIROMENT: 'WORKER'
    //}).then(m => {
      //delete m.then;
      //resolve(m);
    //})
  //);

  //return TdModule;
//}

async function loadTdLib(mode) {
// Uncomment for asmjs support
  //if (mode === 'asmjs') {
    //return loadTdLibAsmjs();
  //}
  return loadTdLibWasm();
}

class OutboundFileSystem {
  constructor(root, FS) {
    this.root = root;
    this.nextFileId = 0;
    this.FS = FS;
    this.files = new Set();
    FS.mkdir(root);
  }
  blobToPath(blob, name) {
    var dir = this.root + '/' + this.nextFileId;
    if (!name) {
      name = 'blob';
    }
    this.nextFileId++;
    this.FS.mkdir(dir);
    this.FS.mount(
      this.FS.filesystems.WORKERFS,
      {
        blobs: [{ name: name, data: blob }]
      },
      dir
    );
    let path = dir + '/' + name;
    this.files.add(path);
    return path;
  }

  forgetPath(path) {
    if (this.files.has(path)) {
      this.FS.unmount(path);
      this.files.delete(path);
    }
  }
}

class InboundFileSystem {
  static async create(dbName, root, FS) {
    try {
      let ifs = new InboundFileSystem();
      ifs.root = root;
      ifs.FS = FS;
      FS.mkdir(root);

      ifs.store = localforage.createInstance({
        name: dbName,
        driver: localForageDrivers
      });
      let keys = await ifs.store.keys();

      ifs.pids = new Set(keys);
      return ifs;
    } catch (e) {
      log.error('Failed to init Inbound FileSystem: ', e);
    }
  }

  has(pid) {
    return this.pids.has(pid);
  }

  forget(pid) {
    this.pids.delete(pid);
  }

  async persist(pid, path) {
    var arr;
    try {
      arr = this.FS.readFile(path);
      await this.store.setItem(pid, new Blob([arr]));
      this.pids.add(pid);
      this.FS.unlink(path);
    } catch (e) {
      log.error('Failed persist ' + path + ' ', e);
    }
    return arr;
  }
}

class DbFileSystem {
  static async create(root, FS, readOnly = false) {
    try {
      let dbfs = new DbFileSystem();
      dbfs.root = root;
      dbfs.FS = FS;
      dbfs.syncfs_total_time = 0;
      dbfs.readOnly = readOnly;
      FS.mkdir(root);
      FS.mount(FS.filesystems.IDBFS, {}, root);

      await new Promise((resolve, reject) => {
        FS.syncfs(true, err => {
          resolve();
        });
      });

      dbfs.syncfsInterval = setInterval(() => {
        dbfs.sync();
      }, 5000);
      return dbfs;
    } catch (e) {
      log.error('Failed to init DbFileSystem: ', e);
    }
  }
  async sync() {
    if (this.readOnly) {
      return;
    }
    let start = performance.now();
    await new Promise((resolve, reject) => {
      this.FS.syncfs(false, () => {
        let syncfs_time = (performance.now() - start) / 1000;
        this.syncfs_total_time += syncfs_time;
        log.debug('SYNC: ' + syncfs_time);
        log.debug('SYNC total: ' + this.syncfs_total_time);
        resolve();
      });
    });
  }
  async close() {
    clearInterval(this.syncfsInterval);
    await this.sync();
  }
}

class TdFileSystem {
  static async create(prefix, FS, readOnly = false) {
    try {
      let tdfs = new TdFileSystem();
      tdfs.prefix = prefix;
      tdfs.FS = FS;
      FS.mkdir(prefix);

      //WORKERFS. Temporary stores Blobs for outbound files
      tdfs.outboundFileSystem = new OutboundFileSystem(
        prefix + '/outboundfs',
        FS
      );

      //MEMFS. Store to IDB and delete files as soon as possible
      let inboundFileSystem = InboundFileSystem.create(
        prefix,
        prefix + '/inboundfs',
        FS
      );

      //IDBFS. MEMFS which is flushed to IDB from time to time
      let dbFileSystem = DbFileSystem.create(prefix + '/dbfs', FS, readOnly);

      tdfs.inboundFileSystem = await inboundFileSystem;
      tdfs.dbFileSystem = await dbFileSystem;
      return tdfs;
    } catch (e) {
      log.error('Failed to init TdFileSystem: ', e);
    }
  }
}

class TdClient {
  constructor(callback) {
    log.info('Start worker');
    this.pendingQueries = [];
    this.isPending = true;
    this.callback = callback;
    this.wasInit = false;
  }

  async testLocalForage() {
    await initLocalForage();
    var DRIVERS = [
      localforage.INDEXEDDB,
      'memoryDriver',
      localforage.LOCALSTORAGE,
      localforage.WEBSQL,
      localForageDrivers
    ];
    for (const driverName of DRIVERS) {
      console.log("Test ", driverName);
      try {
        await localforage.setDriver(driverName);
        console.log("A");
        await localforage.setItem('hello', 'world');
        console.log("B");
        let x = await localforage.getItem('hello');
        console.log("got ", x);
        await localforage.clear();
        console.log("C");
      } catch (error)  {
        console.log("Error", error);
      }
    };
  }

  async init(options) {
    if (this.wasInit) {
      return;
    }
    await this.testLocalForage();
    log.setVerbosity(options.jsVerbosity);
    this.wasInit = true;

    options = options || {};
    let mode = 'wasm';
    if (browser && (browser.name === 'chrome' || browser.name === 'safari')) {
      mode = 'asmjs';
    }
    mode = options.mode || mode;

    this.TdModule = await loadTdLib(mode);
    log.info('got TdModule');
    this.td_functions = {
      td_create: this.TdModule.cwrap('td_create', 'number', []),
      td_destroy: this.TdModule.cwrap('td_destroy', null, ['number']),
      td_send: this.TdModule.cwrap('td_send', null, ['number', 'string']),
      td_execute: this.TdModule.cwrap('td_execute', 'string', [
        'number',
        'string'
      ]),
      td_receive: this.TdModule.cwrap('td_receive', 'string', ['number']),
      td_set_verbosity: verbosity => {
        this.td_functions.td_execute(
          0,
          JSON.stringify({
            '@type': 'setLogVerbosityLevel',
            new_verbosity_level: verbosity
          })
        );
      },
      td_get_timeout: this.TdModule.cwrap('td_get_timeout', 'number', [])
    };
    this.FS = this.TdModule.FS;
    this.TdModule['websocket']['on']('error', error => {
      this.scheduleReceiveSoon();
    });
    this.TdModule['websocket']['on']('open', fd => {
      this.scheduleReceiveSoon();
    });
    this.TdModule['websocket']['on']('listen', fd => {
      this.scheduleReceiveSoon();
    });
    this.TdModule['websocket']['on']('connection', fd => {
      this.scheduleReceiveSoon();
    });
    this.TdModule['websocket']['on']('message', fd => {
      this.scheduleReceiveSoon();
    });
    this.TdModule['websocket']['on']('close', fd => {
      this.scheduleReceiveSoon();
    });

    // wait till it is allowed to start
    this.callback({ '@type': 'inited' });
    var self = this;
    await new Promise(resolve => {
      self.onStart = resolve;
    });
    this.isStarted = true;

    log.info('may start now');
    if (this.isClosing) {
      return;
    }
    let prefix = options.prefix || 'tdlib';
    log.info('FS start init');
    this.tdfs = await TdFileSystem.create(
      '/' + prefix,
      this.FS,
      options.readOnly
    );
    log.info('FS inited');

    // no async initialization after this point
    if (options.verbosity === undefined) {
      options.verbosity = 5;
    }
    this.td_functions.td_set_verbosity(options.verbosity);
    this.client = this.td_functions.td_create();

    this.savingFiles = new Map();
    this.send({
      '@type': 'setOption',
      name: 'language_pack_database_path'
      value: {
        '@type': 'optionValueString',
        value: this.tdfs.dbFileSystem.root + '/language'
      }
    });

    this.flushPendingQueries();

    this.receive();
    //setInterval(()=>this.receive(), 100);
  }

  prepareQueryRecursive(query) {
    if (query['@type'] === 'inputFileBlob') {
      return {
        '@type': 'inputFileLocal',
        path: this.tdfs.outboundFileSystem.blobToPath(query.blob, query.name)
      };
    }
    for (var key in query) {
      let field = query[key];
      if (field && typeof field === 'object') {
        query[key] = this.prepareQueryRecursive(field);
      }
    }
    return query;
  }

  prepareQuery(query) {
    if (query['@type'] === 'setTdlibParameters') {
      query.parameters.database_directory = this.tdfs.dbFileSystem.root;
      query.parameters.files_directory = this.tdfs.inboundFileSystem.root;
    }
    if (query['@type'] === 'getLanguagePackString') {
      query.language_pack_database_path = this.tdfs.dbFileSystem.root + '/language';
    }
    return this.prepareQueryRecursive(query);
  }

  onStart() {
    //nop
    log.info('ignore on_start');
  }

  send(query) {
    if (this.wasFatalError || this.isClosing) {
      return;
    }
    if (query['@type'] === 'init') {
      this.init(query.options);
      return;
    }
    if (query['@type'] === 'start') {
      log.info('on_start');
      this.onStart();
      return;
    }
    if (query['@type'] === 'setJsVerbosity') {
      log.setVerbosity(query.verbosity);
      return;
    }
    if (query['@type'] === 'setVerbosity') {
      this.td_functions.td_set_verbosity(query.verbosity);
      return;
    }
    if (this.isPending) {
      this.pendingQueries.push(query);
      return;
    }
    query = this.prepareQuery(query);
    this.td_functions.td_send(this.client, JSON.stringify(query));
    this.scheduleReceiveSoon();
  }

  receive() {
    this.cancelReceive();
    if (this.wasFatalError) {
      return;
    }
    try {
      while (true) {
        let msg = this.td_functions.td_receive(this.client);
        if (!msg) {
          break;
        }
        let response = this.prepareResponse(JSON.parse(msg));
        if (
          response['@type'] === 'updateAuthorizationState' &&
          response.authorization_state['@type'] === 'authorizationStateClosed'
        ) {
          this.close(response);
          break;
        }
        this.callback(response);
      }

      this.scheduleReceive();
    } catch (error) {
      this.onFatalError(error);
    }
  }

  cancelReceive() {
    if (this.receiveTimeout) {
      clearTimeout(this.receiveTimeout);
      delete this.receiveTimeout;
    }
    delete this.receiveSoon;
  }
  scheduleReceiveSoon() {
    if (this.receiveSoon) {
      return;
    }
    this.cancelReceive();
    this.receiveSoon = true;
    this.scheduleReceiveIn(0.001);
  }
  scheduleReceive() {
    if (this.receiveSoon) {
      return;
    }
    this.cancelReceive();
    let timeout = this.td_functions.td_get_timeout();
    this.scheduleReceiveIn(timeout);
  }
  scheduleReceiveIn(timeout) {
    //return;
    log.debug('Scheduler receive in ' + timeout + 's');
    this.receiveTimeout = setTimeout(() => this.receive(), timeout * 1000);
  }

  onFatalError(error) {
    this.wasFatalError = true;
    this.asyncOnFatalError(error);
  }

  async close(last_update) {
    // close db and cancell all timers
    this.isClosing = true;
    if (this.isStarted) {
      log.debug('close worker: start');
      await this.tdfs.dbFileSystem.close();
      this.cancelReceive();
      log.debug('close worker: finish');
    }
    this.callback(last_update);
  }

  async asyncOnFatalError(error) {
    await this.tdfs.dbFileSystem.sync();
    this.callback({ '@type': 'updateFatalError', error: error });
  }

  async saveFile(pid, file) {
    let isSaving = this.savingFiles.has(pid);
    this.savingFiles.set(pid, file);
    if (isSaving) {
      return;
    }
    let arr = await this.tdfs.inboundFileSystem.persist(pid, file.local.path);
    file = this.savingFiles.get(pid);
    file.idb_key = pid;
    if (arr) {
      file.arr = arr;
    }
    this.callback({ '@type': 'updateFile', file: file }, [arr.buffer]);
    delete file.arr;
    this.savingFiles.delete(pid);
  }

  prepareFile(file) {
    let pid = file.remote.id;
    if (!pid) {
      return file;
    }

    if (file.local.is_downloading_active) {
      this.tdfs.inboundFileSystem.forget(pid);
    } else if (this.tdfs.inboundFileSystem.has(pid)) {
      file.idb_key = pid;
      return file;
    }

    if (file.local.is_downloading_completed) {
      this.saveFile(pid, file);
    }
    return file;
  }

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

  flushPendingQueries() {
    this.isPending = false;
    for (let query of this.pendingQueries) {
      this.send(query);
    }
  }
}

var client = new TdClient((e, t = []) => postMessage(e, t));

onmessage = function(e) {
  try {
    client.send(e.data);
  } catch (error) {
    client.onFatalError(error);
  }
};
