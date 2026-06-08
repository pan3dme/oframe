module.exports = {
  testEnvironment: 'node',
  testMatch: ['**/*.test.js'],
  // WeChat mini program: Page/wx are globals, we mock them in tests
  clearMocks: false,
  resetModules: true,
};
