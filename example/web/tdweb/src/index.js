import MyWorker from './worker.js';
import './third_party/broadcastchannel.js';
import uuid4 from 'uuid/v4';
import log from './logger.js';

const sleep = ms => new Promise(res => setTimeout(res, ms));

class TdClient {
  constructor(options) {
    log.setVerbosity(options.jsVerbosity);
    this.worker = new MyWorker();
    var self = this;
    this.worker.onmessage = function(e) {
      let response = e.data;
      log.info(
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
    this.worker.postMessage({ '@type': 'init', options: options });
    this.closeOtherClients(options);
  }

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

  onWaitSetEmpty() {
    // nop
  }

  onInited() {
    this.isInited = true;
    this.doSendStart();
  }
  sendStart() {
    this.wantSendStart = true;
    this.doSendStart();
  }

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

  onClosed() {
    this.isClosing = true;
    this.worker.terminate();
    log.info('worker is terminated');
    this.state = 'closed';
    this.postState();
  }

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

  onUpdate(response) {
    log.info('ignore onUpdate');
    //nop
  }

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
    if (query['@type'] === 'setJsVerbosity') {
      log.setVerbosity(query.verbosity);
    }

    log.info('send to worker: ', query);
    this.worker.postMessage(query);
    return new Promise((resolve, reject) => {
      this.query_callbacks.set(this.query_id, [resolve, reject]);
    });
  }
}
export default TdClient;
