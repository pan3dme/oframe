from ..core.Vector2D import Vector2D
from ..core.Vector3D import Vector3D
from ..core.Matrix3D import Matrix3D
import glm


class Camera3D(Vector3D):
    def __init__(self):
        super().__init__()
        # self.camRoV2 = Vector2D(0, 30);
        self.rotationX = -30;
        self.rotationY = -90;
        self.downCamRoV2 = Vector2D();
        self.camDis = -400;
        self.downPos = Vector2D(0, 0);
        self.isLastDonw = False;
        self.isMiddleDown = False;
        self.middleDownPos = Vector2D(0, 0);
        self.middleDownLocaAtPos = None;

        self.camMatrix: Matrix3D = Matrix3D()
        self.viewMatrixMatrix3D: Matrix3D = Matrix3D()
        self.vpMatrix3D: Matrix3D = Matrix3D()
        self.widWidth=800
        self.widHeight =600


        self.locaAtPos: Vector3D | None = None

        self.upData()

    def upData(self):
        self.camMatrix.identity()
        self.camMatrix.appendRotation(self.rotationY, Vector3D.Y_AXIS)
        self.camMatrix.appendRotation(self.rotationX, Vector3D.X_AXIS)
        self.camMatrix.appendTranslation(0, 0, -self.camDis)
        if self.locaAtPos is not None:
            self.camMatrix.prependTranslation(self.locaAtPos.x,self.locaAtPos.y,self.locaAtPos.z)

        self.viewMatrixMatrix3D.m = glm.perspectiveLH(1,self.widWidth/ self.widHeight, 10, 5000)


        self.vpMatrix3D = self.viewMatrixMatrix3D.clone();
        self.vpMatrix3D.prepend(self.camMatrix);

        self.meshCamPos()

        pass

    def meshCamPos(self):
        m: Matrix3D = self.camMatrix.clone();
        m.invert();
        p: Vector3D = m.transformVector(Vector3D(0, 0, -self.camDis));
        self.x = p.x;
        self.y = p.y;
        self.z = p.z;
        pass

    def screenToRay(self, screen_x, screen_y):
        """将屏幕坐标转换为世界空间射线方向，返回(射线原点, 射线方向)"""
        import glm
        # 屏幕坐标转NDC
        ndc_x = (2.0 * screen_x / self.widWidth) - 1.0
        ndc_y = 1.0 - (2.0 * screen_y / self.widHeight)

        # NDC -> 裁剪空间
        clip_near = glm.vec4(ndc_x, ndc_y, -1.0, 1.0)
        clip_far = glm.vec4(ndc_x, ndc_y, 1.0, 1.0)

        # 逆VP矩阵
        vp = self.viewMatrixMatrix3D.m * self.camMatrix.m
        inv_vp = glm.inverse(vp)

        # 裁剪空间 -> 世界空间
        world_near = inv_vp * clip_near
        world_far = inv_vp * clip_far

        # 透视除法
        if abs(world_near.w) > 0.001:
            world_near = world_near / world_near.w
        if abs(world_far.w) > 0.001:
            world_far = world_far / world_far.w

        origin = world_near
        direction = glm.normalize(world_far - world_near)

        return origin, direction

    def screenToWorldOnPlane(self, screen_x, screen_y, plane_y=0.0):
        """将屏幕坐标投射到Y=plane_y平面上，返回世界坐标Vector3D，无交点返回None"""
        origin, direction = self.screenToRay(screen_x, screen_y)

        # 射线与Y=plane_y平面求交: origin.y + t * direction.y = plane_y
        if abs(direction.y) < 0.0001:
            return None  # 射线与平面平行

        t = (plane_y - origin.y) / direction.y
        if t < 0:
            return None  # 交点在相机后方

        hit_x = origin.x + t * direction.x
        hit_z = origin.z + t * direction.z

        return Vector3D(hit_x, plane_y, hit_z)

