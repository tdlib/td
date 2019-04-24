import localforage from 'localforage';
import log from './logger.js';
import { instantiateAny } from './wasm-utils.js';

import td_wasm_release from './prebuilt/release/td_wasm.wasm';

// Uncomment for asmjs support
import td_asmjs_mem_release from './prebuilt/release/td_asmjs.js.mem';

const tdlibVersion = 6;
const localForageDrivers = [
  localforage.INDEXEDDB,
  localforage.LOCALSTORAGE,
  'memoryDriver'
];

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
      console.log('getItem', this._map, key, value);
      return value;
    },
    iterate: async function(iteratorCallback) {
      log.error('iterate is not supported');
    },
    key: async function(n) {
      log.error('key n is not supported');
    },
    keys: async function() {
      return this._map.keys();
    },
    length: async function() {
      return this._map.size();
    },
    removeItem: async function(key) {
      this._map.delete(key);
    },
    setItem: async function(key, value) {
      let originalValue = this._map.get(key);
      console.log('setItem', this._map, key, value);
      this._map.set(key, value);
      return originalValue;
    }
  };

  // Add the driver to localForage.
  localforage.defineDriver(memoryDriver);
}

async function loadTdLibWasm(onFS) {
  console.log('loadTdLibWasm');
  let Module = await import('./prebuilt/release/td_wasm.js');
  log.info('got td_wasm.js');
  let td_wasm = td_wasm_release;
  let module = Module.default({
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
  });
  log.info('Got module', module);
  onFS(module.FS);
  let TdModule = new Promise((resolve, reject) =>
    module.then(m => {
      delete m.then;
      resolve(m);
    })
  );

  return TdModule;
}

// Uncomment for asmjs support
async function loadTdLibAsmjs(onFS) {
  console.log('loadTdLibAsmjs');
  let Module = await import('./prebuilt/release/td_asmjs.js');
  console.log('got td_asm.js');
  let fromFile = 'td_asmjs.js.mem';
  let toFile = td_asmjs_mem_release;
  let module = Module({
    onRuntimeInitialized: () => {
      console.log('runtime intialized');
    },
    locateFile: name => {
      if (name === fromFile) {
        return toFile;
      }
      return name;
    },
    ENVIROMENT: 'WORKER'
  });
  onFS(module.FS);
  let TdModule = new Promise((resolve, reject) =>
    module.then(m => {
      delete m.then;
      resolve(m);
    })
  );

  return TdModule;
}

async function loadTdLib(mode, onFS) {
  const wasmSupported = (() => {
    try {
      if (
        typeof WebAssembly === 'object' &&
        typeof WebAssembly.instantiate === 'function'
      ) {
        const module = new WebAssembly.Module(
          Uint8Array.of(0x0, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00)
        );
        if (module instanceof WebAssembly.Module)
          return (
            new WebAssembly.Instance(module) instanceof WebAssembly.Instance
          );
      }
    } catch (e) {}
    return false;
  })();
  if (!wasmSupported) {
    if (mode === 'wasm') {
      log.error('WebAssembly is not supported, trying to use it anyway');
    } else {
      log.warning('WebAssembly is not supported, trying to use asmjs');
      mode = 'asmjs';
    }
  }

  if (mode === 'asmjs') {
    return loadTdLibAsmjs(onFS);
  }
  return loadTdLibWasm(onFS);
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
  static async create(dbName, root, FS_promise) {
    let start = performance.now();
    try {
      let ifs = new InboundFileSystem();
      ifs.root = root;

      ifs.store = localforage.createInstance({
        name: dbName,
        driver: localForageDrivers
      });

      ifs.load_pids();

      let FS = await FS_promise;
      ifs.FS = FS;
      ifs.FS.mkdir(root);
      let create_time = (performance.now() - start) / 1000;
      log.debug('InboundFileSystem::create ' + create_time);
      return ifs;
    } catch (e) {
      log.error('Failed to init Inbound FileSystem: ', e);
    }
  }

  async load_pids() {
    let keys_start = performance.now();
    log.debug('InboundFileSystem::create::keys start');
    let keys = await this.store.keys();
    let keys_time = (performance.now() - keys_start) / 1000;
    log.debug(
      'InboundFileSystem::create::keys ' + keys_time + ' ' + keys.length
    );
    this.pids = new Set(keys);
  }

  has(pid) {
    if (!this.pids) {
      return true;
    }

    return this.pids.has(pid);
  }

  forget(pid) {
    if (this.pids) {
      this.pids.delete(pid);
    }
  }

  async persist(pid, path, arr) {
    try {
      await this.store.setItem(pid, new Blob([arr]));
      if (this.pids) {
        this.pids.add(pid);
      }
      this.FS.unlink(path);
    } catch (e) {
      log.error('Failed persist ' + path + ' ', e);
    }
  }
}

class DbFileSystem {
  static async create(root, FS_promise, readOnly = false) {
    let start = performance.now();
    try {
      let dbfs = new DbFileSystem();
      dbfs.root = root;
      let FS = await FS_promise;
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

      let rmrf = path => {
        log.debug('rmrf ', path);
        var info;
        try {
          info = FS.lookupPath(path);
        } catch (e) {
          return;
        }
        log.debug('rmrf ', path, info);
        if (info.node.isFolder) {
          for (var key in info.node.contents) {
            rmrf(info.path + '/' + info.node.contents[key].name);
          }
          log.debug('rmdir ', path);
          FS.rmdir(path);
        } else {
          log.debug('unlink ', path);
          FS.unlink(path);
        }
      };
      //var dirs = ['thumbnails', 'profile_photos', 'secret', 'stickers', 'temp', 'wallpapers', 'secret_thumbnails', 'passport'];
      var dirs = [];
      let root_dir = FS.lookupPath(root);
      for (var key in root_dir.node.contents) {
        let value = root_dir.node.contents[key];
        log.debug('node ', key, value);
        if (!value.isFolder) {
          continue;
        }
        dirs.push(root_dir.path + '/' + value.name);
      }
      for (let i in dirs) {
        let dir = dirs[i];
        rmrf(dir);
        //FS.mkdir(dir);
        //FS.mount(FS.filesystems.MEMFS, {}, dir);
      }
      dbfs.syncfsInterval = setInterval(() => {
        dbfs.sync();
      }, 5000);
      let create_time = (performance.now() - start) / 1000;
      log.debug('DbFileSystem::create ' + create_time);
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
  async destroy() {
    clearInterval(this.syncfsInterval);
    if (this.readOnly) {
      return;
    }
    this.FS.unmount(this.root);
    var req = indexedDB.deleteDatabase(this.root);
    await new Promise((resolve, reject) => {
      req.onsuccess = function(e) {
        log.info('SUCCESS');
        resolve(e.result);
      };
      req.onerror = function(e) {
        log.info('ONERROR');
        reject(e.error);
      };
      req.onblocked = function(e) {
        log.info('ONBLOCKED');
        reject('blocked');
      };
    });
  }
}

class TdFileSystem {
  static async init_fs(prefix, FS_promise) {
    let FS = await FS_promise;
    FS.mkdir(prefix);
    return FS;
  }
  static async create(instanceName, FS_promise, readOnly = false) {
    try {
      let tdfs = new TdFileSystem();
      let prefix = '/' + instanceName;
      tdfs.prefix = prefix;
      FS_promise = TdFileSystem.init_fs(prefix, FS_promise);

      //MEMFS. Store to IDB and delete files as soon as possible
      let inboundFileSystem = InboundFileSystem.create(
        instanceName,
        prefix + '/inboundfs',
        FS_promise
      );

      //IDBFS. MEMFS which is flushed to IDB from time to time
      let dbFileSystem = DbFileSystem.create(
        prefix + '/dbfs',
        FS_promise,
        readOnly
      );

      let FS = await FS_promise;
      tdfs.FS = FS;

      //WORKERFS. Temporary stores Blobs for outbound files
      tdfs.outboundFileSystem = new OutboundFileSystem(
        prefix + '/outboundfs',
        tdfs.FS
      );

      tdfs.inboundFileSystem = await inboundFileSystem;
      tdfs.dbFileSystem = await dbFileSystem;
      return tdfs;
    } catch (e) {
      log.error('Failed to init TdFileSystem: ', e);
    }
  }
  async destroy() {
    await this.dbFileSystem.destroy();
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
      console.log('Test ', driverName);
      try {
        await localforage.setDriver(driverName);
        console.log('A');
        await localforage.setItem('hello', 'world');
        console.log('B');
        let x = await localforage.getItem('hello');
        console.log('got ', x);
        await localforage.clear();
        console.log('C');
      } catch (error) {
        console.log('Error', error);
      }
    }
  }

  async init(options) {
    if (this.wasInit) {
      return;
    }
    //await this.testLocalForage();
    log.setVerbosity(options.jsLogVerbosityLevel);
    this.wasInit = true;

    options = options || {};
    let mode = 'wasm';
    mode = options.mode || mode;

    var self = this;
    let FS_promise = new Promise(resolve => {
      self.onFS = resolve;
    });

    let tdfs_promise = TdFileSystem.create(
      options.instanceName,
      FS_promise,
      options.readOnly
    );

    this.useDatabase = true;
    if ('useDatabase' in options) {
      this.useDatabase = options.useDatabase;
    }

    log.info('load TdModule');
    this.TdModule = await loadTdLib(mode, self.onFS);
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
    //this.onFS(this.TdModule.FS);
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
    await new Promise(resolve => {
      self.onStart = resolve;
    });
    this.isStarted = true;

    log.info('may start now');
    if (this.isClosing) {
      return;
    }
    log.info('FS start init');
    this.tdfs = await tdfs_promise;
    log.info('FS inited');

    // no async initialization after this point
    if (options.logVerbosityLevel === undefined) {
      options.logVerbosityLevel = 2;
    }
    this.td_functions.td_set_verbosity(options.logVerbosityLevel);
    this.client = this.td_functions.td_create();

    this.savingFiles = new Map();
    this.send({
      '@type': 'setOption',
      name: 'language_pack_database_path',
      value: {
        '@type': 'optionValueString',
        value: this.tdfs.dbFileSystem.root + '/language'
      }
    });
    this.send({
      '@type': 'setOption',
      name: 'ignore_background_updates',
      value: {
        '@type': 'optionValueBoolean',
        value: !this.useDatabase
      }
    });

    this.flushPendingQueries();

    this.receive();
  }

  prepareQueryRecursive(query) {
    if (query['@type'] === 'inputFileBlob') {
      return {
        '@type': 'inputFileLocal',
        path: this.tdfs.outboundFileSystem.blobToPath(query.data, query.name)
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

      let useDb = this.useDatabase;
      query.parameters.use_file_database = useDb;
      query.parameters.use_chat_info_database = useDb;
      query.parameters.use_message_database = useDb;
      query.parameters.use_secret_chats = useDb;
    }
    if (query['@type'] === 'getLanguagePackString') {
      query.language_pack_database_path =
        this.tdfs.dbFileSystem.root + '/language';
    }
    return this.prepareQueryRecursive(query);
  }

  onStart() {
    //nop
    log.info('ignore on_start');
  }

  readFilePart(query) {
    var res;
    try {
      //let file_size = this.FS.stat(query.path).size;
      var stream = this.FS.open(query.path, 'r');
      var buf = new Uint8Array(query.size);
      this.FS.read(stream, buf, 0, query.size, query.offset);
      this.FS.close(stream);
      res = buf;
    } catch (e) {
      this.callback({
        '@type': 'error',
        '@extra': query['@extra'],
        code: 400,
        message: e
      });
      return;
    }
    this.callback(
      {
        '@type': 'FilePart',
        '@extra': query['@extra'],
        data: res
      },
      [res.buffer]
    );
  }

  send(query) {
    if (this.isClosing) {
      return;
    }
    if (this.wasFatalError) {
      if (query['@type'] === 'destroy') {
        this.destroy({ '@type': 'Ok', '@extra': query['@extra'] });
      }
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
    if (query['@type'] === 'setJsLogVerbosityLevel') {
      log.setVerbosity(query.new_verbosity_level);
      return;
    }
    if (this.isPending) {
      this.pendingQueries.push(query);
      return;
    }
    if (
      query['@type'] === 'setLogVerbosityLevel' ||
      query['@type'] === 'getLogVerbosityLevel' ||
      query['@type'] === 'setLogTagVerbosityLevel' ||
      query['@type'] === 'getLogTagVerbosityLevel' ||
      query['@type'] === 'getLogTags'
    ) {
      this.execute(query);
      return;
    }
    if (query['@type'] === 'readFilePart') {
      this.readFilePart(query);
      return;
    }
    query = this.prepareQuery(query);
    this.td_functions.td_send(this.client, JSON.stringify(query));
    this.scheduleReceiveSoon();
  }

  execute(query) {
    try {
      let res = this.td_functions.td_execute(0, JSON.stringify(query));
      let response = JSON.parse(res);
      this.callback(response);
    } catch (error) {
      this.onFatalError(error);
    }
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

  async destroy(result) {
    try {
      log.info('destroy tdfs ...');
      await this.tdfs.destroy();
      log.info('destroy tdfs ok');
    } catch (e) {
      log.error('Failed destroy', e);
    }
    this.callback(result);
    this.callback({
      '@type': 'updateAuthorizationState',
      authorization_state: {
        '@type': 'authorizationStateClosed'
      }
    });
  }

  async asyncOnFatalError(error) {
    await this.tdfs.dbFileSystem.sync();
    this.callback({ '@type': 'updateFatalError', error: error });
  }

  saveFile(pid, file) {
    let isSaving = this.savingFiles.has(pid);
    this.savingFiles.set(pid, file);
    if (isSaving) {
      return file;
    }
    try {
      var arr = this.FS.readFile(file.local.path);
      if (arr) {
        file = Object.assign({}, file);
        file.arr = arr;
        this.doSaveFile(pid, file, arr);
      }
    } catch (e) {
      log.error('Failed to readFile: ', e);
    }
    return file;
  }

  async doSaveFile(pid, file, arr) {
    await this.tdfs.inboundFileSystem.persist(pid, file.local.path, arr);
    file = this.savingFiles.get(pid);
    file.idb_key = pid;
    this.callback({ '@type': 'updateFile', file: file });

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
      file = this.saveFile(pid, file);
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
