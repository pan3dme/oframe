#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
from tablestore import OTSClient


def main():
    try:
        # 从环境变量中获取访问凭证（需要配置TABLESTORE_ACCESS_KEY_ID和TABLESTORE_ACCESS_KEY_SECRET）
        access_key_id = os.getenv("ALIYUN_ACCESS_KEY_ID")
        access_key_secret = os.getenv("ALIYUN_ACCESS_KEY_SECRET")

        # TODO: 根据实例信息修改以下配置
        instance_name = "tabel001"  # 填写实例名称
        endpoint = "https://tabel001.cn-shanghai.ots.aliyuncs.com"  # 填写实例访问地址

        # 创建客户端实例
        client = OTSClient(endpoint, access_key_id, access_key_secret, instance_name)

        # 列举数据表
        resp = client.list_table()

        print(f"在实例 '{instance_name}' 中共找到 {len(resp)} 个数据表:")
        for table_name in resp:
            print(f"{table_name}")

        # 列举时序表
        resp = client.list_timeseries_table()

        print(f"\n在实例 '{instance_name}' 中共找到 {len(resp)} 个时序表:")
        for tableMeta in resp:
            print(f"{tableMeta.timeseries_table_name}")

    except Exception as e:
        print(f"操作失败: {str(e)}")
        sys.exit(1)


if __name__ == "__main__":
    main()