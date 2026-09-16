import 'dart:convert';
import 'package:flutter/material.dart';
import 'package:http/http.dart' as http;
import '../utils/db_helper.dart';
import '../main.dart'; // 导入全局 globalWechatId

/// FC 登录接口地址
const String _loginFcUrl = 'https://gpsmoveinfo.cn/fc/device';

/// 登录页面
class LoginPage extends StatefulWidget {
  const LoginPage({super.key});

  @override
  State<LoginPage> createState() => _LoginPageState();
}

class _LoginPageState extends State<LoginPage> {
  final TextEditingController _usernameController = TextEditingController();
  final TextEditingController _passwordController = TextEditingController();
  bool _autoLogin = false;
  bool _isLoading = true;

  @override
  void initState() {
    super.initState();
    _checkAutoLogin();
  }

  /// 检查自动登录
  Future<void> _checkAutoLogin() async {
    try {
      final autoLogin = await DBHelper().getBoolSetting('auto_login');
      final username = await DBHelper().getSetting('login_username') ?? '';
      final password = await DBHelper().getSetting('login_password') ?? '';
      final wechatid = await DBHelper().getSetting('login_wechatid') ?? '';

      if (autoLogin && username.isNotEmpty && password.isNotEmpty && wechatid.isNotEmpty) {
        // 自动登录：恢复全局 wechatid，直接跳转主页
        globalWechatId = wechatid;
        debugPrint('[登录] 自动登录，wechatid=$globalWechatId');
        if (mounted) {
          Navigator.of(context).pushReplacementNamed('/home');
        }
        return;
      }

      // 不需要自动登录，显示登录表单
      if (mounted) {
        setState(() {
          _autoLogin = autoLogin;
          if (username.isNotEmpty) {
            _usernameController.text = username;
          }
          if (password.isNotEmpty) {
            _passwordController.text = password;
          }
          _isLoading = false;
        });
      }
    } catch (e) {
      debugPrint('[登录] 检查自动登录失败: $e');
      if (mounted) {
        setState(() => _isLoading = false);
      }
    }
  }

  /// 执行登录（HTTP 请求服务器）
  Future<void> _doLogin() async {
    final username = _usernameController.text.trim();
    final password = _passwordController.text.trim();

    if (username.isEmpty) {
      _showError('请输入用户名');
      return;
    }
    if (password.isEmpty) {
      _showError('请输入密码');
      return;
    }

    setState(() => _isLoading = true);

    try {
      final resp = await http.post(
        Uri.parse(_loginFcUrl),
        headers: {'Content-Type': 'application/json'},
        body: jsonEncode({
          'action': 'login',
          'info': {
            'username': username,
            'password': password,
            'code': '',
          },
        }),
      );

      debugPrint('[登录] 响应状态: ${resp.statusCode}');
      debugPrint('[登录] 响应body: ${resp.body}');

      if (resp.statusCode == 200) {
        final json = jsonDecode(resp.body) as Map<String, dynamic>;

        if (json['status'] == 'success') {
          // 从服务器响应中提取 wechatid
          // 响应格式: data.attributes[{columnName, columnValue}]
          final data = json['data'] as Map<String, dynamic>?;
          String wechatid = '';
          final attributes = data?['attributes'] as List<dynamic>?;
          if (attributes != null) {
            for (final attr in attributes) {
              if (attr is Map<String, dynamic> && attr['columnName'] == 'wechatid') {
                wechatid = attr['columnValue']?.toString() ?? '';
                break;
              }
            }
          }
          // 兼容旧格式：直接 data.wechatid
          if (wechatid.isEmpty) {
            wechatid = data?['wechatid']?.toString() ?? '';
          }

          if (wechatid.isEmpty) {
            setState(() => _isLoading = false);
            _showError('登录失败：服务器未返回wechatid');
            return;
          }

          // 设置全局 wechatid
          globalWechatId = wechatid;

          // 保存凭证到本地数据库
          await DBHelper().saveSetting('login_username', username);
          await DBHelper().saveSetting('login_password', password);
          await DBHelper().saveSetting('login_wechatid', wechatid);
          await DBHelper().saveSetting('auto_login', _autoLogin.toString());

          debugPrint('[登录] 登录成功，wechatid=$wechatid');

          if (mounted) {
            Navigator.of(context).pushReplacementNamed('/home');
          }
        } else {
          setState(() => _isLoading = false);
          _showError(json['msg'] ?? '用户名或密码错误');
        }
      } else {
        setState(() => _isLoading = false);
        _showError('网络请求失败，请检查网络连接');
      }
    } catch (e) {
      debugPrint('[登录] 请求异常: $e');
      setState(() => _isLoading = false);
      _showError('网络异常，请稍后重试');
    }
  }

  /// 显示错误提示
  void _showError(String message) {
    if (!mounted) return;
    ScaffoldMessenger.of(context).showSnackBar(
      SnackBar(
        content: Text(message),
        backgroundColor: Colors.red,
        duration: const Duration(seconds: 2),
      ),
    );
  }

  @override
  void dispose() {
    _usernameController.dispose();
    _passwordController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.white,
      body: SafeArea(
        child: _isLoading
            ? const Center(child: CircularProgressIndicator())
            : Center(
                child: SingleChildScrollView(
                  padding: const EdgeInsets.symmetric(horizontal: 32),
                  child: Column(
                    mainAxisAlignment: MainAxisAlignment.center,
                    children: [
                      // Logo / 标题
                      Icon(
                        Icons.devices,
                        size: 80,
                        color: Colors.blue.shade700,
                      ),
                      const SizedBox(height: 16),
                      const Text(
                        '牛羊助手',
                        style: TextStyle(
                          fontSize: 28,
                          fontWeight: FontWeight.bold,
                          color: Colors.black87,
                        ),
                      ),
                      const SizedBox(height: 48),

                      // 用户名输入框
                      TextField(
                        controller: _usernameController,
                        decoration: InputDecoration(
                          labelText: '用户名',
                          prefixIcon: const Icon(Icons.person),
                          border: OutlineInputBorder(
                            borderRadius: BorderRadius.circular(12),
                          ),
                          filled: true,
                          fillColor: Colors.grey.shade50,
                        ),
                        textInputAction: TextInputAction.next,
                      ),
                      const SizedBox(height: 16),

                      // 密码输入框（明码显示）
                      TextField(
                        controller: _passwordController,
                        decoration: InputDecoration(
                          labelText: '密码',
                          prefixIcon: const Icon(Icons.lock),
                          border: OutlineInputBorder(
                            borderRadius: BorderRadius.circular(12),
                          ),
                          filled: true,
                          fillColor: Colors.grey.shade50,
                        ),
                        // 密码显示明码，不设置 obscureText
                        textInputAction: TextInputAction.done,
                        onSubmitted: (_) => _doLogin(),
                      ),
                      const SizedBox(height: 12),

                      // 自动登录复选框
                      Row(
                        children: [
                          SizedBox(
                            width: 24,
                            height: 24,
                            child: Checkbox(
                              value: _autoLogin,
                              onChanged: (value) {
                                setState(() {
                                  _autoLogin = value ?? false;
                                });
                              },
                            ),
                          ),
                          const SizedBox(width: 8),
                          const Text(
                            '下次自动登录',
                            style: TextStyle(fontSize: 14, color: Colors.black54),
                          ),
                        ],
                      ),
                      const SizedBox(height: 24),

                      // 登录按钮
                      SizedBox(
                        width: double.infinity,
                        height: 50,
                        child: ElevatedButton(
                          onPressed: _isLoading ? null : _doLogin,
                          style: ElevatedButton.styleFrom(
                            backgroundColor: Colors.blue,
                            foregroundColor: Colors.white,
                            shape: RoundedRectangleBorder(
                              borderRadius: BorderRadius.circular(12),
                            ),
                          ),
                          child: const Text(
                            '登 录',
                            style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
                          ),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
      ),
    );
  }
}
