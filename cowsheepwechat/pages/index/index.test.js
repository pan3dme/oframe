// index.test.js - Unit tests for pages/index/index.js

// ===== Mock wx global APIs (must be before require source) =====
global.wx = {
  request: jest.fn(),
  showToast: jest.fn(),
  showModal: jest.fn(),
  showLoading: jest.fn(),
  hideLoading: jest.fn(),
  openBluetoothAdapter: jest.fn(),
  onBluetoothDeviceFound: jest.fn(),
  startBluetoothDevicesDiscovery: jest.fn(),
  stopBluetoothDevicesDiscovery: jest.fn(),
  createBLEConnection: jest.fn(),
  getBLEDeviceServices: jest.fn(),
  getBLEDeviceCharacteristics: jest.fn(),
  notifyBLECharacteristicValueChange: jest.fn(),
  onBLECharacteristicValueChange: jest.fn(),
  writeBLECharacteristicValue: jest.fn(),
  closeBluetoothAdapter: jest.fn(),
};

// Mock console to reduce noise
global.console = {
  log: jest.fn(),
  error: jest.fn(),
  warn: jest.fn(),
};

// ===== Capture Page options =====
let pageOptions;
global.Page = jest.fn(function (options) {
  pageOptions = options;
});

// Now require the source file
require('./index.js');

// ===== Helper to create a fresh page instance =====
function createPageInstance() {
  const instance = {
    data: JSON.parse(JSON.stringify(pageOptions.data)),
    setData: jest.fn(function (obj) {
      Object.assign(this.data, obj);
    }),
  };

  // Bind all methods from pageOptions to this instance
  Object.keys(pageOptions).forEach((key) => {
    if (typeof pageOptions[key] === 'function') {
      instance[key] = pageOptions[key].bind(instance);
    }
  });

  return instance;
}

// ===== Tests =====
describe('pages/index/index.js', () => {
  beforeEach(() => {
    // Reset all wx mock call history
    jest.clearAllMocks();
  });

  describe('Page initialization', () => {
    it('should call Page() with valid options', () => {
      expect(global.Page).toHaveBeenCalledTimes(1);
      expect(pageOptions).toBeDefined();
      expect(pageOptions.data).toBeDefined();
    });

    it('should have featureBtns with correct labels', () => {
      expect(pageOptions.data.featureBtns).toEqual([
        { id: 1, label: '获取一条记录' },
        { id: 2, label: '插入一条记录' },
        { id: 3, label: '删除一条记录' },
        { id: 4, label: '功能四' },
        { id: 5, label: '功能五' },
        { id: 6, label: '功能六' },
        { id: 7, label: '功能七' },
        { id: 8, label: '功能八' },
        { id: 9, label: '功能九' },
      ]);
    });

    it('should have postIdCounter starting at 2', () => {
      expect(pageOptions.data.postIdCounter).toBe(2);
    });

    it('should have bluetoothConnected default to false', () => {
      expect(pageOptions.data.bluetoothConnected).toBe(false);
    });

    it('should have bluetoothScanning default to false', () => {
      expect(pageOptions.data.bluetoothScanning).toBe(false);
    });

    it('should initialize with empty devices array', () => {
      expect(pageOptions.data.devices).toEqual([]);
    });

    it('should initialize with empty receivedMsg', () => {
      expect(pageOptions.data.receivedMsg).toBe('');
    });
  });

  // ===== Utility Functions =====
  describe('abToHex()', () => {
    let page;

    beforeEach(() => {
      page = createPageInstance();
    });

    it('should return empty string for empty buffer', () => {
      const result = page.abToHex(new ArrayBuffer(0));
      expect(result).toBe('');
    });

    it('should return empty string for null/undefined buffer', () => {
      expect(page.abToHex(null)).toBe('');
      expect(page.abToHex(undefined)).toBe('');
    });

    it('should convert single byte to hex', () => {
      const buffer = new Uint8Array([255]).buffer;
      expect(page.abToHex(buffer)).toBe('ff');
    });

    it('should convert multiple bytes to space-separated hex', () => {
      const buffer = new Uint8Array([0x48, 0x65, 0x6c, 0x6c, 0x6f]).buffer; // "Hello"
      expect(page.abToHex(buffer)).toBe('48 65 6c 6c 6f');
    });

    it('should pad single-digit hex values with zero', () => {
      const buffer = new Uint8Array([0x0a, 0x01, 0x00]).buffer;
      expect(page.abToHex(buffer)).toBe('0a 01 00');
    });

    it('should handle all zero bytes', () => {
      const buffer = new Uint8Array([0, 0, 0]).buffer;
      expect(page.abToHex(buffer)).toBe('00 00 00');
    });
  });

  describe('textToAb()', () => {
    let page;

    beforeEach(() => {
      page = createPageInstance();
    });

    it('should return empty ArrayBuffer for empty text', () => {
      const result = page.textToAb('');
      expect(result.byteLength).toBe(0);
    });

    it('should return empty ArrayBuffer for null/undefined', () => {
      expect(page.textToAb(null).byteLength).toBe(0);
      expect(page.textToAb(undefined).byteLength).toBe(0);
    });

    it('should convert single character to ArrayBuffer', () => {
      const result = page.textToAb('A');
      const view = new Uint8Array(result);
      expect(view[0]).toBe(0x41); // 'A' = 65
      expect(view.length).toBe(1);
    });

    it('should convert string to ArrayBuffer', () => {
      const result = page.textToAb('Hello');
      const view = new Uint8Array(result);
      expect(view.length).toBe(5);
      expect(view[0]).toBe('H'.charCodeAt(0));
      expect(view[4]).toBe('o'.charCodeAt(0));
    });

    it('should handle special characters', () => {
      const result = page.textToAb('!@#');
      const view = new Uint8Array(result);
      expect(view.length).toBe(3);
    });
  });

  describe('abToText()', () => {
    let page;

    beforeEach(() => {
      page = createPageInstance();
    });

    it('should return empty string for empty buffer', () => {
      expect(page.abToText(new ArrayBuffer(0))).toBe('');
    });

    it('should return empty string for null/undefined buffer', () => {
      expect(page.abToText(null)).toBe('');
      expect(page.abToText(undefined)).toBe('');
    });

    it('should decode ASCII text from ArrayBuffer', () => {
      const buffer = new Uint8Array([72, 101, 108, 108, 111]).buffer; // "Hello"
      const result = page.abToText(buffer);
      expect(result).toBe('Hello');
    });

    it('should fall back to HEX for non-printable binary data', () => {
      // Binary data with non-printable chars and no Chinese/letters/numbers
      const buffer = new Uint8Array([0x00, 0x01, 0x02]).buffer;
      const result = page.abToText(buffer);
      expect(result).toMatch(/^HEX: /);
    });

    it('should decode digits and letters correctly', () => {
      const buffer = new Uint8Array([49, 50, 51]).buffer; // "123"
      const result = page.abToText(buffer);
      expect(result).toBe('123');
    });
  });

  // ===== onUnload =====
  describe('onUnload()', () => {
    it('should call closeBluetoothAdapter', () => {
      const page = createPageInstance();
      page.disconnectBluetooth = jest.fn();

      page.onUnload();

      expect(page.disconnectBluetooth).toHaveBeenCalledTimes(1);
    });
  });

  // ===== onFeatureTap =====
  describe('onFeatureTap()', () => {
    let page;

    beforeEach(() => {
      page = createPageInstance();
      // Reset counter to known state
      page.data.postIdCounter = 2;
    });

    // --- ID = 1 (获取一条记录 / getrow) ---
    describe('id = 1 (getrow)', () => {
      it('should send POST request with action="getrow" and info.id=4', () => {
        page.onFeatureTap({ currentTarget: { dataset: { id: 1 } } });

        expect(wx.request).toHaveBeenCalledTimes(1);
        const callArgs = wx.request.mock.calls[0][0];

        expect(callArgs.url).toBe('https://fuck-cor-renifsfpao.cn-shanghai.fcapp.run/');
        expect(callArgs.method).toBe('POST');
        expect(callArgs.data.action).toBe('getrow');
        expect(callArgs.data.info).toEqual({ id: 4 });
      });

      it('should use postIdCounter as id in postData', () => {
        page.data.postIdCounter = 5;

        page.onFeatureTap({ currentTarget: { dataset: { id: 1 } } });

        const callArgs = wx.request.mock.calls[0][0];
        expect(callArgs.data.id).toBe(5);
      });

      it('should increment postIdCounter after call', () => {
        expect(page.data.postIdCounter).toBe(2);

        page.onFeatureTap({ currentTarget: { dataset: { id: 1 } } });

        expect(page.data.postIdCounter).toBe(3);
      });

      it('should include time in postData', () => {
        page.onFeatureTap({ currentTarget: { dataset: { id: 1 } } });

        const callArgs = wx.request.mock.calls[0][0];
        expect(callArgs.data.time).toBeDefined();
        expect(typeof callArgs.data.time).toBe('string');
      });

      it('should show success toast on request success', () => {
        wx.request.mockImplementation((opts) => {
          opts.success({ data: { result: 'ok' } });
        });

        page.onFeatureTap({ currentTarget: { dataset: { id: 1 } } });

        expect(wx.showToast).toHaveBeenCalledWith({
          title: '提交成功',
          icon: 'success',
          duration: 1500,
        });
      });

      it('should set receivedMsg on request success', () => {
        wx.request.mockImplementation((opts) => {
          opts.success({ data: { result: 'test-data' } });
        });

        page.onFeatureTap({ currentTarget: { dataset: { id: 1 } } });

        expect(page.setData).toHaveBeenCalledWith({
          receivedMsg: '{"result":"test-data"}',
        });
      });

      it('should show error toast on request failure', () => {
        wx.request.mockImplementation((opts) => {
          opts.fail({ errMsg: 'network error' });
        });

        page.onFeatureTap({ currentTarget: { dataset: { id: 1 } } });

        expect(wx.showToast).toHaveBeenCalledWith({
          title: '提交失败',
          icon: 'error',
          duration: 2000,
        });
      });
    });

    // --- ID = 2 (插入一条记录 / insert) ---
    describe('id = 2 (insert)', () => {
      it('should send POST request with action="insert"', () => {
        page.onFeatureTap({ currentTarget: { dataset: { id: 2 } } });

        const callArgs = wx.request.mock.calls[0][0];
        expect(callArgs.data.action).toBe('insert');
      });

      it('should include info with currentId and url', () => {
        page.data.postIdCounter = 2;

        page.onFeatureTap({ currentTarget: { dataset: { id: 2 } } });

        const callArgs = wx.request.mock.calls[0][0];
        expect(callArgs.data.info).toEqual({
          id: 2,
          url: 'baidu.com',
        });
      });

      it('should increment postIdCounter', () => {
        expect(page.data.postIdCounter).toBe(2);

        page.onFeatureTap({ currentTarget: { dataset: { id: 2 } } });

        expect(page.data.postIdCounter).toBe(3);
      });

      it('should use incremented counter on subsequent calls', () => {
        // First call: counter 2 → 3
        page.onFeatureTap({ currentTarget: { dataset: { id: 2 } } });
        expect(wx.request.mock.calls[0][0].data.info.id).toBe(2);

        // Second call: counter 3 → 4
        page.onFeatureTap({ currentTarget: { dataset: { id: 2 } } });
        expect(wx.request.mock.calls[1][0].data.info.id).toBe(3);

        // Third call: counter 4 → 5
        page.onFeatureTap({ currentTarget: { dataset: { id: 2 } } });
        expect(wx.request.mock.calls[2][0].data.info.id).toBe(4);
      });
    });

    // --- ID = 3 (删除一条记录) ---
    describe('id = 3 (delete)', () => {
      it('should send POST request even without special handling', () => {
        page.onFeatureTap({ currentTarget: { dataset: { id: 3 } } });

        expect(wx.request).toHaveBeenCalledTimes(1);
        const callArgs = wx.request.mock.calls[0][0];
        expect(callArgs.method).toBe('POST');
      });

      it('should have action="" when no special handler exists', () => {
        page.onFeatureTap({ currentTarget: { dataset: { id: 3 } } });

        const callArgs = wx.request.mock.calls[0][0];
        expect(callArgs.data.action).toBe('');
      });

      it('should not have info property when no handler exists', () => {
        page.onFeatureTap({ currentTarget: { dataset: { id: 3 } } });

        const callArgs = wx.request.mock.calls[0][0];
        expect(callArgs.data.info).toBeUndefined();
      });
    });

    // --- ID = 9 (other) ---
    describe('id = 9 (other/unhandled)', () => {
      it('should still send POST request with default action=""', () => {
        page.onFeatureTap({ currentTarget: { dataset: { id: 9 } } });

        const callArgs = wx.request.mock.calls[0][0];
        expect(callArgs.data.action).toBe('');
        expect(callArgs.method).toBe('POST');
      });
    });

    // --- Cross-cutting concerns ---
    describe('cross-cutting', () => {
      it('should always use the same URL', () => {
        page.onFeatureTap({ currentTarget: { dataset: { id: 1 } } });
        page.onFeatureTap({ currentTarget: { dataset: { id: 2 } } });

        const url1 = wx.request.mock.calls[0][0].url;
        const url2 = wx.request.mock.calls[1][0].url;
        expect(url1).toBe(url2);
      });

      it('should always use POST method regardless of id', () => {
        [1, 2, 3, 4, 5, 6, 7, 8, 9].forEach((id) => {
          page.onFeatureTap({ currentTarget: { dataset: { id } } });
          const lastCall = wx.request.mock.calls[wx.request.mock.calls.length - 1][0];
          expect(lastCall.method).toBe('POST');
        });
      });

      it('should share postIdCounter across different feature button ids', () => {
        // id=1: uses counter 2, increments to 3
        page.onFeatureTap({ currentTarget: { dataset: { id: 1 } } });
        expect(wx.request.mock.calls[0][0].data.id).toBe(2);

        // id=2: uses counter 3, increments to 4
        page.onFeatureTap({ currentTarget: { dataset: { id: 2 } } });
        expect(wx.request.mock.calls[1][0].data.id).toBe(3);

        // id=1 again: uses counter 4, increments to 5
        page.onFeatureTap({ currentTarget: { dataset: { id: 1 } } });
        expect(wx.request.mock.calls[2][0].data.id).toBe(4);
      });
    });
  });

  // ===== disconnectBluetooth =====
  describe('disconnectBluetooth()', () => {
    it('should call closeBluetoothAdapter', () => {
      const page = createPageInstance();

      wx.closeBluetoothAdapter.mockImplementation((opts) => {
        if (opts.success) opts.success();
      });

      page.disconnectBluetooth();

      expect(wx.closeBluetoothAdapter).toHaveBeenCalledTimes(1);
    });

    it('should reset bluetooth state on success', () => {
      const page = createPageInstance();
      page.data.bluetoothConnected = true;
      page.data.receivedMsg = 'test msg';

      wx.closeBluetoothAdapter.mockImplementation((opts) => {
        if (opts.success) opts.success();
      });

      page.disconnectBluetooth();

      expect(page.setData).toHaveBeenCalledWith({
        bluetoothConnected: false,
        connectedDeviceName: '',
        receivedMsg: '',
        writeDeviceInfo: null,
        isSyncing: false,
      });
    });
  });
});
