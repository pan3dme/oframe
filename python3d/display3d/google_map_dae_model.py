import os

import collada


from display3d.area_map_display_sprite import AreaMapDisplay3DSprite
from pan3d.core.Vector3D import Vector3D


class GoogleMapDaeModel:
    """专门用于解析和统计Google Earth DAE模型的类"""

    def __init__(self, scene3d):
        """
        初始化GoogleMapDaeModel

        Args:
            scene3d: 3D场景对象
        """
        self.scene3d = scene3d
        self.dae_objects = []  # 存储所有DAE对象信息
        self.model_stats = {}  # 模型统计信息
        self.dae_path = None  # DAE文件路径
        self.collada_mesh = None  # Collada mesh对象

    def load_dae_model(self, dae_path):
        """
        加载DAE文件并统计模型信息

        Args:
            dae_path: DAE文件路径
        """
        self.dae_path = dae_path

        try:
            # 使用collada库加载DAE文件
            self.collada_mesh = collada.Collada(dae_path)

            # 解析模型信息
            self._parse_model_info()

            # 打印统计信息
            # self._print_model_stats()

            print(f"成功加载DAE文件: {dae_path}")
            for i, obj in enumerate(self.model_stats['objects']):

                self._makeBaseDaeObjToScene(obj['name'])



            return True

        except Exception as e:
            print(f"加载DAE文件失败: {e}")
            return False

    def _parse_model_info(self):
        """解析DAE模型信息"""
        self.dae_objects = []
        self.model_stats = {
            'object_count': 0,
            'objects': [],
            'total_vertices': 0,
            'total_triangles': 0,
            'total_textures': 0,
            'texture_names': set(),
            'images': [],
            'materials': []
        }

        # 解析图像/纹理信息
        try:
            for image in self.collada_mesh.images:
                # 处理图像路径，可能是相对路径
                image_path = image.path if hasattr(image, 'path') else str(image)
                image_info = {
                    'id': image.id if hasattr(image, 'id') else f'image_{len(self.model_stats["images"])}',
                    'path': image_path,
                    'name': os.path.basename(image_path) if image_path else f'image_{len(self.model_stats["images"])}'
                }
                self.model_stats['images'].append(image_info)
                self.model_stats['texture_names'].add(image_path)
        except Exception as e:
            print(f"解析图像信息时出错: {e}")

        self.model_stats['total_textures'] = len(self.model_stats['images'])

        # 解析材质信息
        try:
            for mat in self.collada_mesh.materials:
                mat_info = {
                    'id': mat.id if hasattr(mat, 'id') else str(mat),
                    'name': mat.name if hasattr(mat, 'name') else str(mat)
                }
                self.model_stats['materials'].append(mat_info)
        except Exception as e:
            print(f"解析材质信息时出错: {e}")

        # 解析几何体信息
        for geometry in self.collada_mesh.geometries:
            obj_info = {
                'name': geometry.name if hasattr(geometry, 'name') else str(geometry),
                'id': geometry.id if hasattr(geometry, 'id') else str(geometry),
                'vertices': 0,
                'triangles': 0,
                'normals': 0,
                'texcoords': 0,
                'textures': [],
                'primitives': []
            }

            # 统计顶点和三角形
            for primitive in geometry.primitives:
                prim_info = {
                    'type': type(primitive).__name__,
                    'vertex_count': len(primitive.vertex) if hasattr(primitive, 'vertex') and primitive.vertex is not None else 0,
                    'index_count': len(primitive.index) if hasattr(primitive, 'index') and primitive.index is not None else 0,
                    'has_texcoord': len(primitive.texcoordset) > 0 if hasattr(primitive, 'texcoordset') and primitive.texcoordset else False,
                    'has_normal': primitive.normal is not None if hasattr(primitive, 'normal') else False
                }

                obj_info['vertices'] += prim_info['vertex_count']

                # 计算三角形数量
                if hasattr(primitive, 'index') and primitive.index is not None:
                    obj_info['triangles'] += len(primitive.index)
                else:
                    # 如果没有索引，顶点数/3就是三角形数
                    obj_info['triangles'] += prim_info['vertex_count'] // 3

                # 统计法线
                if hasattr(primitive, 'normal') and primitive.normal is not None:
                    obj_info['normals'] += len(primitive.normal)

                # 统计纹理坐标
                if hasattr(primitive, 'texcoordset') and primitive.texcoordset:
                    obj_info['texcoords'] += len(primitive.texcoordset[0])

                # 获取材质信息
                if hasattr(primitive, 'material') and primitive.material:
                    mat = primitive.material
                    mat_id = mat.id if hasattr(mat, 'id') else str(mat)
                    if mat_id:
                        obj_info['textures'].append(mat_id)

                obj_info['primitives'].append(prim_info)

            # 添加到对象列表
            self.dae_objects.append(obj_info)
            self.model_stats['objects'].append(obj_info)

            # 更新总计
            self.model_stats['total_vertices'] += obj_info['vertices']
            self.model_stats['total_triangles'] += obj_info['triangles']

        self.model_stats['object_count'] = len(self.dae_objects)

    def load_google_earth_model(self):
        """
        加载Google Earth模型
        使用方法: 在初始化完成后调用此方法
        """
        # 构建DAE文件路径
        current_dir = os.path.dirname(os.path.abspath(__file__))
        dae_path = os.path.join(current_dir, '..', 'res', 'blandermap', '005', '005.dae')
        dae_path = os.path.normpath(dae_path)

        # 检查文件是否存在
        if not os.path.exists(dae_path):
            print(f"错误: DAE文件不存在: {dae_path}")
            return False

        print(f"正在加载DAE文件: {dae_path}")
        return self.load_dae_model(dae_path)



    def get_vertex_data(self, geometry_name=None):
        """
        获取顶点数据

        Args:
            geometry_name: 几何体名称，如果为None则返回所有顶点数据

        Returns:
            dict: 包含顶点数据的字典，包括顶点、法线、纹理坐标和索引
        """
        if not self.collada_mesh:
            return None

        result = {}

        for geometry in self.collada_mesh.geometries:
            if geometry_name and geometry.name != geometry_name:
                continue

            geo_data = {
                'name': geometry.name,
                'vertices': [],
                'normals': [],
                'texcoords': [],
                'indices': []
            }

            for primitive in geometry.primitives:
                # 获取顶点数据
                if hasattr(primitive, 'vertex') and primitive.vertex is not None:
                    geo_data['vertices'].extend(primitive.vertex.tolist())

                # 获取法线数据
                if hasattr(primitive, 'normal') and primitive.normal is not None:
                    geo_data['normals'].extend(primitive.normal.tolist())

                # 获取纹理坐标数据
                if hasattr(primitive, 'texcoordset') and primitive.texcoordset:
                    geo_data['texcoords'].extend(primitive.texcoordset[0].tolist())

                # 获取索引数据
                if hasattr(primitive, 'index') and primitive.index is not None:
                    geo_data['indices'].extend(primitive.index.tolist())

            result[geometry.name] = geo_data

        return result

    def get_texture_paths(self):
        """
        获取所有纹理路径

        Returns:
            list: 纹理路径列表
        """
        if not self.collada_mesh:
            return []

        return [img.path for img in self.collada_mesh.images]

    def get_material_info(self):
        """
        获取材质信息

        Returns:
            list: 材质信息列表
        """
        if not self.collada_mesh:
            return []

        materials = []
        for mat in self.collada_mesh.materials:
            mat_info = {
                'id': mat.id,
                'name': mat.name,
                'effect_id': mat.effect.id if mat.effect else None
            }
            materials.append(mat_info)

        return materials

    def get_geometry_material_info(self, geometry_name):
        """
        获取特定几何体使用的材质信息和图片地址

        Args:
            geometry_name: 几何体名称

        Returns:
            dict: 包含材质信息和图片地址的字典，格式如下:
                {
                    'material_id': 材质ID,
                    'material_name': 材质名称,
                    'texture_path': 纹理图片路径,
                    'texture_name': 纹理图片名称
                }
            如果未找到材质信息，返回None
        """
        if not self.collada_mesh:
            return None
            
        # 查找指定的几何体
        target_geometry = None
        for geometry in self.collada_mesh.geometries:
            if geometry.name == geometry_name:
                target_geometry = geometry
                break
                
        if not target_geometry:
            return None
            
        # 遍历几何体的所有图元，查找材质信息
        for primitive in target_geometry.primitives:
            if hasattr(primitive, 'material') and primitive.material:
                mat = primitive.material
                
                # 获取材质信息
                material_info = {
                    'material_id': mat.id if hasattr(mat, 'id') else '',
                    'material_name': mat.name if hasattr(mat, 'name') else ''
                }
                
                # 获取纹理信息
                if hasattr(mat, 'effect') and mat.effect:
                    effect = mat.effect
                    # 遍历效果的所有参数，查找纹理
                    for param_name, param_value in effect.params.items():
                        # 检查是否是纹理参数
                        if hasattr(param_value, 'sampler'):
                            sampler = param_value.sampler
                            # 获取纹理图片
                            if hasattr(sampler, 'surface') and sampler.surface:
                                surface = sampler.surface
                                if hasattr(surface, 'image') and surface.image:
                                    image = surface.image
                                    image_path = image.path if hasattr(image, 'path') else ''
                                    material_info['texture_path'] = image_path
                                    material_info['texture_name'] = os.path.basename(image_path) if image_path else ''
                                    break
                
                return material_info
                
        return None
        
    def get_geometry_texture_path(self, geometry_name):
        """
        获取特定几何体使用的纹理图片路径

        Args:
            geometry_name: 几何体名称

        Returns:
            str: 纹理图片路径，如果未找到则返回None
        """
        if not self.collada_mesh:
            return None
            
        # 查找指定的几何体
        target_geometry = None
        for geometry in self.collada_mesh.geometries:
            if geometry.name == geometry_name:
                target_geometry = geometry
                break
                
        if not target_geometry:
            return None
            
        # print(f"\n=== 查找几何体 {geometry_name} 的纹理 ===")
        
        # 遍历几何体的所有图元，查找材质信息
        for primitive in target_geometry.primitives:
            # print(f"图元类型: {type(primitive).__name__}")
            
            # 检查是否有纹理坐标
            # if hasattr(primitive, 'texcoordset') and primitive.texcoordset:
                # print(f"找到纹理坐标集: {len(primitive.texcoordset)}")
            
            # 获取材质
            if hasattr(primitive, 'material') and primitive.material:
                mat = primitive.material
                # print(f"材质: {mat}, 类型: {type(mat).__name__}")
                
                # 如果材质是字符串，则从collada_mesh.materials中查找
                if isinstance(mat, str):
                    # print(f"从collada_mesh.materials中查找材质: {mat}")
                    if mat in self.collada_mesh.materials:
                        material = self.collada_mesh.materials[mat]
                        # print(f"找到材质对象: {material}")
                        # print(f"材质属性: {dir(material)}")
                        
                        # 获取纹理信息
                        if hasattr(material, 'effect') and material.effect:
                            effect = material.effect
                            # print(f"找到效果: {effect}")
                            # print(f"效果类型: {effect.shadingtype if hasattr(effect, 'shadingtype') else '无'}")
                            
                            # 尝试从diffuse属性获取纹理
                            if hasattr(effect, 'diffuse') and effect.diffuse:
                                diffuse = effect.diffuse
                                # print(f"找到diffuse: {diffuse}, 类型: {type(diffuse).__name__}")
                                
                                # 检查diffuse是否是纹理
                                if hasattr(diffuse, 'sampler'):
                                    sampler = diffuse.sampler
                                    # print(f"找到采样器: {sampler}")
                                    # 获取纹理图片
                                    if hasattr(sampler, 'surface') and sampler.surface:
                                        surface = sampler.surface
                                        # print(f"找到表面: {surface}")
                                        if hasattr(surface, 'image') and surface.image:
                                            image = surface.image
                                            # print(f"找到图片: {image}, 路径: {image.path if hasattr(image, 'path') else '无路径'}")
                                            image_path = image.path if hasattr(image, 'path') else ''
                                            return image_path
                                
                                # 检查diffuse是否是Map类型
                                if hasattr(diffuse, 'texture'):
                                    texture = diffuse.texture
                                    # print(f"找到纹理: {texture}")
                                    # 获取纹理图片
                                    if hasattr(texture, 'sampler'):
                                        sampler = texture.sampler
                                        # print(f"找到采样器: {sampler}")
                                        if hasattr(sampler, 'surface') and sampler.surface:
                                            surface = sampler.surface
                                            # print(f"找到表面: {surface}")
                                            if hasattr(surface, 'image') and surface.image:
                                                image = surface.image
                                                # print(f"找到图片: {image}, 路径: {image.path if hasattr(image, 'path') else '无路径'}")
                                                image_path = image.path if hasattr(image, 'path') else ''
                                                return image_path
                                    
                                    # 尝试直接从texture获取图片
                                    if hasattr(texture, 'image'):
                                        image = texture.image
                                        print(f"找到图片: {image}, 路径: {image.path if hasattr(image, 'path') else '无路径'}")
                                        image_path = image.path if hasattr(image, 'path') else ''
                                        return image_path
                            
                            # 尝试从params列表获取纹理
                            if hasattr(effect, 'params') and effect.params:
                                # print(f"找到params列表，数量: {len(effect.params)}")
                                for param in effect.params:
                                    # print(f"参数: {param}, 类型: {type(param).__name__}")
                                    if hasattr(param, 'sampler'):
                                        sampler = param.sampler
                                        # print(f"找到采样器: {sampler}")
                                        if hasattr(sampler, 'surface') and sampler.surface:
                                            surface = sampler.surface
                                            # print(f"找到表面: {surface}")
                                            if hasattr(surface, 'image') and surface.image:
                                                image = surface.image
                                                # print(f"找到图片: {image}, 路径: {image.path if hasattr(image, 'path') else '无路径'}")
                                                image_path = image.path if hasattr(image, 'path') else ''
                                                return image_path
        
        return None
        
    def print_all_geometry_textures(self):
        """
        打印所有几何体的纹理信息
        """
        if not self.collada_mesh:
            print("未加载DAE文件")
            return
            
        # print(f"\n=== 所有几何体的纹理信息 ===")
        # print(f"几何体数量: {len(self.collada_mesh.geometries)}")
        
        # 遍历所有几何体
        for geometry in self.collada_mesh.geometries:
            # print(f"\n几何体: {geometry.name}")
            
            # 遍历几何体的所有图元
            for primitive in geometry.primitives:
                # print(f"  图元类型: {type(primitive).__name__}")
                
                # 获取材质
                if hasattr(primitive, 'material') and primitive.material:
                    mat = primitive.material
                    # print(f"  材质: {mat.id if hasattr(mat, 'id') else '无ID'}, {mat.name if hasattr(mat, 'name') else '无名称'}")
                    
                    # 获取纹理信息
                    if hasattr(mat, 'effect') and mat.effect:
                        effect = mat.effect
                        # print(f"  效果: {effect.id if hasattr(effect, 'id') else '无ID'}, 参数数量: {len(effect.params)}")
                        
                        # 遍历效果的所有参数，查找纹理
                        for param_name, param_value in effect.params.items():
                            # print(f"    参数: {param_name}, 类型: {type(param_value).__name__}")
                            # 检查是否是纹理参数
                            if hasattr(param_value, 'sampler'):
                                sampler = param_value.sampler
                                # print(f"    找到采样器: {sampler}")
                                # 获取纹理图片
                                if hasattr(sampler, 'surface') and sampler.surface:
                                    surface = sampler.surface
                                    # print(f"    找到表面: {surface}")
                                    if hasattr(surface, 'image') and surface.image:
                                        image = surface.image
                                        # print(f"    找到图片: {image}, 路径: {image.path if hasattr(image, 'path') else '无路径'}")
        
        # 打印所有图片信息
        # print(f"\n=== 所有图片信息 ===")
        # print(f"图片数量: {len(self.collada_mesh.images)}")
        for image in self.collada_mesh.images:
            print(f"图片: {image.id if hasattr(image, 'id') else '无ID'}, 路径: {image.path if hasattr(image, 'path') else '无路径'}")


    def _makeBaseDaeObjToScene(self,name):

        obj= self.get_vertex_data(name)
        picpath = self.get_geometry_texture_path(name)
        dis=AreaMapDisplay3DSprite(self.scene3d)
        dis.setTextureUrl(picpath)


        self.scene3d.camera3D.locaAtPos=Vector3D(0,0,0)

        dis.setDaeInfo(obj[name])

        self.scene3d.addDisplay(dis)



    def get_model_stats(self):
        """
        获取模型统计信息

        Returns:
            dict: 包含所有模型统计信息的字典
        """
        return self.model_stats

    def get_dae_objects(self):
        """
        获取所有DAE对象信息

        Returns:
            list: 所有DAE对象信息列表
        """
        return self.dae_objects

    def get_object_by_name(self, name):
        """
        根据名称获取对象信息

        Args:
            name: 对象名称

        Returns:
            dict: 找到的对象信息，未找到返回None
        """
        for obj in self.dae_objects:
            if obj['name'] == name:
                return obj
        return None

    def get_objects_by_texture(self, texture_path):
        """
        获取使用指定纹理的所有对象

        Args:
            texture_path: 纹理路径

        Returns:
            list: 使用该纹理的对象信息列表
        """
        result = []
        for obj in self.dae_objects:
            if texture_path in obj['textures']:
                result.append(obj)
        return result

    def export_to_json(self, output_path):
        """
        将模型信息导出为JSON文件

        Args:
            output_path: 输出文件路径
        """
        import json

        # 准备导出数据
        export_data = {
            'dae_file': self.dae_path,
            'statistics': self.model_stats,
            'objects': self.dae_objects,
            'materials': self.get_material_info(),
            'textures': self.get_texture_paths()
        }

        # 转换set为list以便JSON序列化
        if 'texture_names' in export_data['statistics']:
            export_data['statistics']['texture_names'] = list(export_data['statistics']['texture_names'])

        # 写入JSON文件
        with open(output_path, 'w', encoding='utf-8') as f:
            json.dump(export_data, f, indent=2, ensure_ascii=False)

        print(f"模型信息已导出到: {output_path}")
