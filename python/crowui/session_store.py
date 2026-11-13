# -*- coding: utf-8 -*-
"""
登录状态持久化（对标小程序 wx.setStorageSync 的 login_info / last_login / setting_auto_login）
存储位置：项目根目录 login_state.json

三份数据的语义与小程序一致：
- auto_login : 自动登录开关（登录页勾选后持久化）
- last_login : 上次的用户名/密码 —— 注销登入后仍保留，用于登录页预填
- login_info : 登录成功后的用户数据 —— 自动登录免密进主界面的依据，注销时清除
"""

import json
import os
import time

_STATE_PATH = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                           'login_state.json')


def _load_all():
    try:
        with open(_STATE_PATH, 'r', encoding='utf-8') as f:
            data = json.load(f)
        return data if isinstance(data, dict) else {}
    except (OSError, ValueError):
        return {}


def _save_all(data):
    try:
        with open(_STATE_PATH, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
    except OSError:
        pass


def get_auto_login():
    return _load_all().get('auto_login') is True


def set_auto_login(value):
    data = _load_all()
    data['auto_login'] = bool(value)
    _save_all(data)


def get_last_login():
    """上次登录的账密（注销后保留，用于预填）；无则返回 None"""
    last = _load_all().get('last_login')
    if isinstance(last, dict) and last.get('username'):
        return {'username': str(last.get('username', '')),
                'password': str(last.get('password', ''))}
    return None


def set_last_login(username, password):
    data = _load_all()
    data['last_login'] = {'username': str(username), 'password': str(password)}
    _save_all(data)


def get_login_info():
    """登录成功后保存的用户数据；无则返回 None"""
    info = _load_all().get('login_info')
    return info if isinstance(info, dict) and info.get('username') else None


def set_login_info(username, password, wechatid, level, mapcenter):
    data = _load_all()
    data['login_info'] = {
        'username': str(username), 'password': str(password),
        'wechatid': str(wechatid), 'level': level,
        'mapcenter': str(mapcenter or ''),
        'login_time': time.strftime('%Y/%m/%d %H:%M:%S'),
        'isLoggedIn': True,
    }
    _save_all(data)


def clear_login():
    """注销登入：清除 login_info（免密依据），保留 last_login 与 auto_login 供预填"""
    data = _load_all()
    data.pop('login_info', None)
    _save_all(data)
