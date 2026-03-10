#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
HMI后端程序
订阅ROS1障碍物检测话题，处理后通过网络发送给前端
支持可视化_msgs/MarkerArray格式
"""

import rospy
import socket
import json
import math
import time
import os
import logging
from datetime import datetime
from geometry_msgs.msg import Pose, Vector3, Quaternion
from std_msgs.msg import Header, ColorRGBA
from visualization_msgs.msg import Marker, MarkerArray
try:
    from jsk_recognition_msgs.msg import BoundingBox, BoundingBoxArray
    HAS_JSK = True
except ImportError:
    HAS_JSK = False
    BoundingBoxArray = None


def setup_logger():
    """配置日志"""
    log_dir = '/home/good/workspace/jili_hmi/source/log'
    os.makedirs(log_dir, exist_ok=True)

    log_file = os.path.join(log_dir, f'hmi_backend_{datetime.now().strftime("%Y%m%d")}.log')

    logger = logging.getLogger('hmi_backend')
    logger.setLevel(logging.INFO)

    # 文件处理器
    file_handler = logging.FileHandler(log_file, encoding='utf-8')
    file_handler.setLevel(logging.INFO)
    file_format = logging.Formatter('%(asctime)s [%(levelname)s] %(message)s')
    file_handler.setFormatter(file_format)

    # 控制台处理器
    console_handler = logging.StreamHandler()
    console_handler.setLevel(logging.INFO)
    console_format = logging.Formatter('[%(levelname)s] %(message)s')
    console_handler.setFormatter(console_format)

    logger.addHandler(file_handler)
    logger.addHandler(console_handler)

    return logger


class HMIBackend:
    def __init__(self):
        # 初始化日志
        self.logger = setup_logger()
        self.logger.info("=" * 50)
        self.logger.info("HMI Backend 启动")
        self.logger.info("=" * 50)

        # 网络配置
        self.hmi_ip = rospy.get_param('~hmi_ip', '192.168.30.218')
        self.hmi_port = rospy.get_param('~hmi_port', 8765)
        self.protocol = rospy.get_param('~protocol', 'udp')

        self.logger.info(f"网络配置: {self.protocol.upper()} -> {self.hmi_ip}:{self.hmi_port}")

        # 自车参数
        self.vehicle_length = 6.5
        self.vehicle_width = 3.15
        self.rear_axle_to_center = 2.25
        self.logger.info(f"自车参数: 长={self.vehicle_length}m, 宽={self.vehicle_width}m, 后轴距={self.rear_axle_to_center}m")

        # 障碍物合并缓存
        self.pending_trucks = {}

        # 报警阈值（米）
        self.alarm_thresholds = {
            'high': 1.5,
            'medium': 5.0,
            'low': 10.0,
            'max': 10.0  # 10米外不考虑
        }
        self.logger.info(f"报警阈值: 高<{self.alarm_thresholds['high']}m, 中<{self.alarm_thresholds['medium']}m, 低<{self.alarm_thresholds['low']}m, 忽略>{self.alarm_thresholds['max']}m")

        # 障碍物跟踪器
        self.tracked_obstacles = {}  # track_id -> {last_position, last_time, stable_count}
        self.next_track_id = 1
        self.track_threshold = 2.0  # 位置匹配阈值（米）
        self.track_timeout = 0.5  # 跟踪超时时间（秒）

        # 语音报警状态跟踪
        self.voice_alarm_history = {}  # track_id -> {'priority': int, 'last_trigger_time': float}
        self.current_voice_alarm = {'track_id': None, 'priority': 0, 'direction': None, 'distance': None, 'type': None}
        self.voice_alarm_cooldown = 30.0  # 同优先级冷却时间（秒）：障碍物持续存在时，每隔此时间重新报警

        # 创建socket
        self.setup_socket()

        # ROS订阅
        topic = rospy.get_param('~obstacle_topic', '/obstacle_detection/ex_det')
        msg_type = rospy.get_param('~msg_type', 'auto')

        # 自动检测或手动指定消息类型
        if msg_type == 'auto':
            # 尝试使用 BoundingBoxArray，如果失败则回退到 MarkerArray
            if HAS_JSK and BoundingBoxArray:
                try:
                    self.sub = rospy.Subscriber(topic, BoundingBoxArray,
                                               self.bbox_callback, queue_size=1)
                    self.logger.info(f"订阅话题: {topic} (BoundingBoxArray)")
                except:
                    self.sub = rospy.Subscriber(topic, MarkerArray,
                                               self.obstacle_callback, queue_size=1)
                    self.logger.info(f"订阅话题: {topic} (MarkerArray)")
            else:
                self.sub = rospy.Subscriber(topic, MarkerArray,
                                           self.obstacle_callback, queue_size=1)
                self.logger.info(f"订阅话题: {topic} (MarkerArray)")
        elif msg_type == 'bbox' and HAS_JSK and BoundingBoxArray:
            self.sub = rospy.Subscriber(topic, BoundingBoxArray,
                                       self.bbox_callback, queue_size=1)
            self.logger.info(f"订阅话题: {topic} (BoundingBoxArray)")
        else:
            self.sub = rospy.Subscriber(topic, MarkerArray,
                                       self.obstacle_callback, queue_size=1)
            self.logger.info(f"订阅话题: {topic} (MarkerArray)")

        # 发布 MarkerArray 用于可视化
        self.visual_pub = rospy.Publisher('~visualization', MarkerArray, queue_size=1)
        self.logger.info(f"发布可视化话题: {self.visual_pub.name}")

        self.logger.info(f"HMI Backend 启动成功")

    def setup_socket(self):
        """创建网络socket"""
        try:
            if self.protocol == 'udp':
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
            else:
                self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.sock.connect((self.hmi_ip, self.hmi_port))
            self.sock.settimeout(1.0)
            self.logger.info(f"Socket 创建成功: {self.protocol.upper()}")
        except Exception as e:
            self.logger.error(f"Socket 创建失败: {e}")
            raise

    def send_data(self, data):
        """发送数据到前端"""
        # 打印发送到前端的数据
        #print("\n" + "="*60)
        #print("[HMI_BACKEND] 发到前端的数据:")
        #print(json.dumps(data, ensure_ascii=False, indent=2))
        #print("="*60)

        try:
            json_data = json.dumps(data, ensure_ascii=False)
            if self.protocol == 'udp':
                self.sock.sendto(json_data.encode(), (self.hmi_ip, self.hmi_port))
            else:
                self.sock.sendall(json_data.encode() + b'\n')
            self.logger.debug(f"发送数据成功: {len(json_data)} bytes")
        except Exception as e:
            self.logger.error(f"发送数据失败: {e}")

    def bbox_callback(self, msg):
        """处理 BoundingBoxArray 消息 (jsk_recognition_msgs)"""
        if not msg.boxes:
            return

        # 初始化缓存（用于车头车挂配对）- 每帧清空，因 indices 是帧内索引
        self.trailer_cab_map = {}

        obstacles = []
        alarms = []
        highest_priority = 0

        # 遍历所有框，先建立车头车挂映射
        for i, bbox in enumerate(msg.boxes):
            label = getattr(bbox, 'label', None)
            if label not in [1, 2, 3]:
                continue

            vx = bbox.pose.position.x
            vy = bbox.pose.position.y
            length = bbox.dimensions.x
            width = bbox.dimensions.y
            height = bbox.dimensions.z

            if label == 1:
                # 车头：查找最近的车挂
                min_dist = float('inf')
                matched_idx = -1
                for j, other in enumerate(msg.boxes):
                    other_label = getattr(other, 'label', None)
                    if other_label not in [2, 3]:
                        continue
                    dist = math.sqrt((vx - other.pose.position.x)**2 + (vy - other.pose.position.y)**2)
                    if dist < min_dist:
                        min_dist = dist
                        matched_idx = j
                if matched_idx >= 0:
                    self.trailer_cab_map[matched_idx] = i
                    self.logger.info(f"[MERGE] 车头(idx={i}) 匹配车挂(idx={matched_idx}), 距离={min_dist:.2f}m")
            elif label in [2, 3]:
                # 车挂：检查是否已被车头匹配
                if i not in self.trailer_cab_map.values():
                    # 没被匹配，查找最近的车头
                    min_dist = float('inf')
                    matched_idx = -1
                    for j, other in enumerate(msg.boxes):
                        other_label = getattr(other, 'label', None)
                        if other_label != 1:
                            continue
                        dist = math.sqrt((vx - other.pose.position.x)**2 + (vy - other.pose.position.y)**2)
                        if dist < min_dist:
                            min_dist = dist
                            matched_idx = j
                    if matched_idx >= 0:
                        self.trailer_cab_map[i] = matched_idx
                        self.logger.info(f"[MERGE] 车挂(idx={i}) 匹配车头(idx={matched_idx}), 距离={min_dist:.2f}m")

        processed_trailer_idxs = set()

        for i, bbox in enumerate(msg.boxes):
            label = getattr(bbox, 'label', None)

            # label=0 过滤
            if label == 0:
                continue

            vx = bbox.pose.position.x
            vy = bbox.pose.position.y
            vz = bbox.pose.position.z
            length = bbox.dimensions.x
            width = bbox.dimensions.y
            height = bbox.dimensions.z

            yaw = self.quaternion_to_yaw(
                bbox.pose.orientation.x, bbox.pose.orientation.y,
                bbox.pose.orientation.z, bbox.pose.orientation.w
            )

            # label=2,3 (车挂): 沿航向向前移动2.37米
            if label in [2, 3]:
                yaw_rad = math.radians(yaw)
                vx = vx + 2.37 * math.cos(yaw_rad)
                vy = vy + 2.37 * math.sin(yaw_rad)

                # 如果车挂已被匹配，标记并跳过（由车头处理合并）
                if i in self.trailer_cab_map:
                    processed_trailer_idxs.add(i)
                    self.logger.info(f"[MERGE] 车挂(idx={i}) 由车头处理")
                    continue

            # 根据 label 分类
            if label in [1, 2, 3]:
                obs_type = 'truck'
            elif label == 6:
                obs_type = 'pedestrian'
            else:
                obs_type = 'other'

            # 创建 fake marker 用于距离计算
            class FakeMarker:
                def __init__(self, x, y, l, w, yaw):
                    self.pose = type('obj', (object,), {'position': type('obj', (object,), {'x': x, 'y': y})(),
                                                         'orientation': type('obj', (object,), {'x': 0, 'y': 0, 'z': math.sin(yaw/2), 'w': math.cos(yaw/2)})()})()
                    self.scale = type('obj', (object,), {'x': l, 'y': w, 'z': 1.0})()
            fake_marker = FakeMarker(vx, vy, length, width, math.radians(yaw))
            distance, direction = self.calc_distance_direction(fake_marker)

            # 先获取track_id用于语音报警状态跟踪
            track_id = self.track_obstacle(obs_type, vx, vy)

            # 计算语音报警
            current_voice_priority, dir_val, dist_val, type_val = self.calc_voice_alarm(distance, direction, obs_type)

            # 检查是否需要触发报警（优先级提升 或 冷却期已过）
            voice_triggered = self._check_voice_trigger(track_id, current_voice_priority, dir_val)

            level, priority = self.calc_alarm_level(distance, direction)
            alarm_text = self.get_alarm_text(distance, direction, obs_type)

            obstacle = {
                'id': f"{obs_type}_{track_id}",
                'type': obs_type,
                'x': round(vx, 2),
                'y': round(vy, 2),
                'z': round(vz, 2),
                'length': round(length, 2),
                'width': round(width, 2),
                'height': round(height, 2),
                'rotationY': round(yaw, 1),
                'distance': round(distance, 2),
                'direction': direction
            }
            obstacles.append(obstacle)

            if level != 'none':
                alarm = {
                    'level': level,
                    'text': alarm_text,
                    'priority': priority,
                    'distance': round(distance, 2),
                    'direction': direction,
                    'type': obs_type
                }
                if voice_triggered:
                    alarm['voice_triggered'] = True
                    alarm['voice_priority'] = current_voice_priority
                    alarm['track_id'] = track_id
                alarms.append(alarm)
                if priority == 'high':
                    highest_priority = max(highest_priority, 3)
                elif priority == 'medium':
                    highest_priority = max(highest_priority, 2)
                elif priority == 'low':
                    highest_priority = max(highest_priority, 1)

        if obstacles:
            for obs in obstacles:
                self.logger.info(f"发送: id={obs['id']}, type={obs['type']}, pos=({obs['x']}, {obs['y']})")
            self.logger.info(f"发送数据: 障碍物={len(obstacles)}, 报警={highest_priority}")

        # 清理过期的语音报警历史记录（障碍物消失）
        current_time = time.time()
        expired_track_ids = []
        for track_id in self.voice_alarm_history.keys():
            if track_id not in self.tracked_obstacles:
                if track_id == self.current_voice_alarm['track_id']:
                    self.current_voice_alarm = {'track_id': None, 'priority': 0, 'direction': None, 'distance': None, 'type': None}
                expired_track_ids.append(track_id)
        for track_id in expired_track_ids:
            del self.voice_alarm_history[track_id]

        # 从 alarms 中直接提取 voice_alarm
        voice_alarm_event = None
        triggered = [a for a in alarms if a.get('voice_triggered')]
        if triggered:
            triggered.sort(key=lambda a: (-a['voice_priority'], a['distance']))
            best = triggered[0]
            self.current_voice_alarm = {
                'track_id': best['track_id'],
                'priority': best['voice_priority'],
                'direction': best['direction'],
                'distance': best['distance'],
                'type': best['type']
            }
            voice_alarm_event = {k: best[k] for k in ('direction', 'distance', 'type')}
            voice_alarm_event['priority'] = best['voice_priority']
            self.logger.info(f"触发新语音报警: {voice_alarm_event}")

        data = {
            'timestamp': time.time(),
            'obstacles': obstacles,
            'alarms': alarms,
            'highest_priority': highest_priority
        }
        if voice_alarm_event is not None:
            data['voice_alarm'] = voice_alarm_event

        self._log_alarm_info(alarms, highest_priority, voice_alarm_event)
        self.send_data(data)
        self.publish_visualization(obstacles)

    def _log_alarm_info(self, alarms, highest_priority, voice_alarm_event):
        """单独打印报警信息"""
        print("\n" + "-" * 50)
        print("[报警信息]")
        print(f"  highest_priority: {highest_priority}")
        #print(f"  alarms({len(alarms)}):")
        for i, a in enumerate(alarms):
            print(f"    [{i}] {a}")
        if voice_alarm_event is not None:
            print(f"  voice_alarm: {voice_alarm_event}")
        else:
            print("  voice_alarm: (无)")
        print("-" * 50)

    def quaternion_to_yaw(self, qx, qy, qz, qw):
        """从四元数提取Y轴旋转角度（度）"""
        siny_cosp = 2.0 * (qw * qz + qx * qy)
        cosy_cosp = 1.0 - 2.0 * (qy * qy + qz * qz)
        yaw = math.atan2(siny_cosp, cosy_cosp)
        return math.degrees(yaw)

    def vehicle_to_display(self, vx, vy, vz):
        """
        车体坐标系转3D显示坐标系
        车体: x向右, y向前, z向上
        显示: x向右, y向上, z向后(负向前)
        """
        return {
            'x': vx,
            'y': vz,
            'z': -vy
        }

    def classify_obstacle(self, label):
        """
        分类障碍物
        label 0: 忽略（过滤掉）
        label 1: 卡车车头
        label 2, 3: 卡车车挂
        label 6: 行人
        其他: 其他
        """
        if label in [1, 2, 3]:
            return 'truck'
        elif label == 6:
            return 'pedestrian'
        else:
            return 'other'

    def get_track_id(self, marker):
        """从Marker获取跟踪ID"""
        ns = marker.ns if hasattr(marker, 'ns') else ''
        marker_id = marker.id

        # 尝试从ns提取ID
        if '_' in ns:
            try:
                return int(ns.split('_')[-1])
            except:
                return marker_id
        return marker_id

    def merge_trucks(self, marker):
        """
        合并卡车车头和车挂
        label=1: 车头
        label=2, 3: 车挂
        判断条件：两个包围盒有重叠（中心距离小于尺寸和的一半）
        """
        label = getattr(marker, 'label', 8)

        # 只处理卡车相关
        if label not in [1, 2, 3]:
            return marker

        # 初始化缓存
        if not hasattr(self, 'truck_cache'):
            self.truck_cache = {'cabs': {}, 'trailers': {}}

        now = rospy.Time.now()

        # 清理过期缓存（超过0.3秒）
        expired_time = 0.3
        expired = [k for k, v in self.truck_cache['cabs'].items()
                   if (now - v['timestamp']).to_sec() > expired_time]
        for k in expired:
            del self.truck_cache['cabs'][k]
        expired = [k for k, v in self.truck_cache['trailers'].items()
                   if (now - v['timestamp']).to_sec() > expired_time]
        for k in expired:
            del self.truck_cache['trailers'][k]

        if label == 1:
            # 车头：保存到缓存，检查是否与车挂重叠
            track_id = self.get_track_id(marker)
            self.truck_cache['cabs'][track_id] = {
                'marker': marker,
                'timestamp': now
            }

            # 检查是否与已有的车挂重叠
            for trail_id, trail_data in list(self.truck_cache.get('trailers', {}).items()):
                trailer = trail_data['marker']
                if self.boxes_overlap(marker, trailer):
                    # 重叠，合并
                    merged = self.do_merge(marker, trailer)
                    del self.truck_cache['trailers'][trail_id]
                    self.logger.debug(f"[MERGE] 车头(label=1)与车挂重叠，合并")
                    return merged

            return marker

        elif label in [2, 3]:
            # 车挂：检查是否与车头重叠
            for cab_id, cab_data in list(self.truck_cache['cabs'].items()):
                cab = cab_data['marker']
                if self.boxes_overlap(cab, marker):
                    # 重叠，合并
                    merged = self.do_merge(cab, marker)
                    del self.truck_cache['cabs'][cab_id]
                    self.logger.debug(f"[MERGE] 车头与车挂(label={label})重叠，合并")
                    return merged

            # 不重叠，保存到缓存
            track_id = self.get_track_id(marker)
            if 'trailers' not in self.truck_cache:
                self.truck_cache['trailers'] = {}
            self.truck_cache['trailers'][track_id] = {
                'marker': marker,
                'timestamp': now
            }
            self.logger.info(f"[MERGE] 不重叠，缓存trailer: id={marker.id}, track_id={track_id}")
            return marker

        return marker

    def boxes_overlap(self, box1, box2):
        """
        判断两个包围盒是否重叠
        简化算法：中心距离 < 车头半长 + 车挂半长 - 重叠阈值
        车头和车挂正常应该连接在一起，重叠距离应该在 1-3 米范围内
        """
        x1, y1 = box1.pose.position.x, box1.pose.position.y
        x2, y2 = box2.pose.position.x, box2.pose.position.y

        # 中心距离
        center_dist = math.sqrt((x2 - x1)**2 + (y2 - y1)**2)

        # 只考虑长度方向（x轴），忽略宽度
        half_len1 = box1.scale.x / 2  # 车头半长
        half_len2 = box2.scale.x / 2  # 车挂半长

        # 理论最小距离 = 车头半长 + 车挂半长 - 理论连接长度(约8-10米)
        # 如果实际距离小于这个值，认为是重叠/连接的
        theoretical_min = half_len1 + half_len2 - 10  # 假设连接长度约10米

        # 实际使用：中心距离 < (车头半长 + 车挂半长 - 2米) 才认为是重叠
        # 即：两车中心距离比"各自半长相加"还近至少2米
        overlap_threshold = half_len1 + half_len2 - 2

        return center_dist < overlap_threshold

    def do_merge(self, cab, trailer):
        """合并车头和车挂的几何信息"""
        cab_vol = cab.scale.x * cab.scale.y * cab.scale.z
        trailer_vol = trailer.scale.x * trailer.scale.y * trailer.scale.z
        total = cab_vol + trailer_vol

        merged = Marker()
        merged.header = cab.header
        merged.id = min(cab.id, trailer.id)
        merged.type = cab.type
        merged.action = cab.action

        merged.pose.position.x = (cab.pose.position.x * cab_vol + trailer.pose.position.x * trailer_vol) / total
        merged.pose.position.y = (cab.pose.position.y * cab_vol + trailer.pose.position.y * trailer_vol) / total
        merged.pose.position.z = (cab.pose.position.z * cab_vol + trailer.pose.position.z * trailer_vol) / total

        # 合并尺寸：车头+车挂沿长度方向相接，长=两者之和；宽高取较大值
        merged.scale.x = cab.scale.x + trailer.scale.x
        merged.scale.y = max(cab.scale.y, trailer.scale.y)
        merged.scale.z = max(cab.scale.z, trailer.scale.z)

        merged.pose.orientation = cab.pose.orientation
        merged.label = 0

        return merged

    def do_bbox_merge(self, cab, trailer):
        """合并两个 BoundingBox"""
        cab_vol = cab.dimensions.x * cab.dimensions.y * cab.dimensions.z
        trailer_vol = trailer.dimensions.x * trailer.dimensions.y * trailer.dimensions.z
        total = cab_vol + trailer_vol

        merged = BoundingBox()
        merged.header = cab.header

        merged.pose.position.x = (cab.pose.position.x * cab_vol + trailer.pose.position.x * trailer_vol) / total
        merged.pose.position.y = (cab.pose.position.y * cab_vol + trailer.pose.position.y * trailer_vol) / total
        merged.pose.position.z = (cab.pose.position.z * cab_vol + trailer.pose.position.z * trailer_vol) / total

        # 合并尺寸：车头+车挂沿长度方向相接
        merged.dimensions.x = cab.dimensions.x + trailer.dimensions.x
        merged.dimensions.y = max(cab.dimensions.y, trailer.dimensions.y)
        merged.dimensions.z = max(cab.dimensions.z, trailer.dimensions.z)

        merged.pose.orientation = cab.pose.orientation

        self.logger.info(f"[MERGE] 合并后: pos=({merged.pose.position.x:.2f}, {merged.pose.position.y:.2f}), size=({merged.dimensions.x:.1f}x{merged.dimensions.y:.1f}x{merged.dimensions.z:.1f})")

        return merged

    def track_obstacle(self, obs_type, x, y):
        """
        跟踪障碍物，返回稳定的 track_id
        基于位置匹配
        """
        current_time = time.time()
        matched_track_id = None
        min_dist = float('inf')

        # 清理超时的障碍物
        expired_ids = []
        for track_id, data in self.tracked_obstacles.items():
            if current_time - data['last_time'] > self.track_timeout:
                expired_ids.append(track_id)
        for tid in expired_ids:
            del self.tracked_obstacles[tid]

        # 尝试匹配现有障碍物
        for track_id, data in self.tracked_obstacles.items():
            if data['type'] != obs_type:
                continue

            dist = math.sqrt((x - data['x'])**2 + (y - data['y'])**2)
            if dist < self.track_threshold and dist < min_dist:
                min_dist = dist
                matched_track_id = track_id

        # 调试日志
        self.logger.debug(f"[TRACK] type={obs_type}, pos=({x:.1f}, {y:.1f}), "
                        f"tracked={list(self.tracked_obstacles.keys())}, matched={matched_track_id}, dist={min_dist:.2f}")

        # 更新或创建
        if matched_track_id is not None:
            self.tracked_obstacles[matched_track_id] = {
                'type': obs_type,
                'x': x,
                'y': y,
                'last_time': current_time,
                'stable_count': self.tracked_obstacles[matched_track_id].get('stable_count', 0) + 1
            }
            return matched_track_id
        else:
            new_id = self.next_track_id
            self.next_track_id += 1
            self.tracked_obstacles[new_id] = {
                'type': obs_type,
                'x': x,
                'y': y,
                'last_time': current_time,
                'stable_count': 1
            }
            return new_id

    def calc_distance_direction(self, marker):
        """计算障碍物与自车的最近距离和方位（考虑尺寸和朝向）"""
        vx = marker.pose.position.x
        vy = marker.pose.position.y

        # 获取障碍物尺寸（长、宽）
        obs_length = marker.scale.x if hasattr(marker, 'scale') else 4.0
        obs_width = marker.scale.y if hasattr(marker, 'scale') else 2.5

        # 障碍物朝向（弧度）
        obs_yaw = self.quaternion_to_yaw(
            marker.pose.orientation.x, marker.pose.orientation.y,
            marker.pose.orientation.z, marker.pose.orientation.w
        )
        obs_yaw_rad = math.radians(obs_yaw)

        # 自车尺寸（长、宽）
        vehicle_l = self.vehicle_length
        vehicle_w = self.vehicle_width

        # 自车位置（假设在原点）
        vehicle_x, vehicle_y = 0, 0
        # 自车朝向：车头朝向 +y 方向
        vehicle_yaw = 0  # 自车车头朝向 y 轴正方向

        # 计算两个旋转矩形之间的最短距离（2D）
        distance = self.calc_rotated_box_distance(
            vehicle_x, vehicle_y, vehicle_l, vehicle_w, vehicle_yaw,
            vx, vy, obs_length, obs_width, obs_yaw_rad
        )

        # 计算方位角（正前方为0度，左正右负）
        # 车体: x=前, y=左 (REP103/FLU)。atan2(vy, vx) 使前方=0°, 左=+90°, 右=-90°
        angle = math.degrees(math.atan2(vy, vx))
        if angle > 180:
            angle -= 360
        elif angle <= -180:
            angle += 360

        # 方位判断（正前方为0度，左正右负）
        if distance > self.alarm_thresholds['max']:
            direction = 'far'
        elif angle > 135 or angle < -135:
            direction = 'behind'
        elif angle > 45:
            direction = 'left'
        elif angle > 10:
            direction = 'left_front'
        elif angle > -10:
            direction = 'front'
        elif angle > -45:
            direction = 'right_front'
        else:
            direction = 'right'

        return distance, direction

    def calc_rotated_box_distance(self, x1, y1, l1, w1, yaw1, x2, y2, l2, w2, yaw2):
        """
        计算两个旋转矩形之间的最短距离（2D）
        x1, y1, l1, w1, yaw1: 第一个矩形（自车）
        x2, y2, l2, w2, yaw2: 第二个矩形（障碍物）
        """
        # 简化为轴对齐矩形距离（如果需要完整SAT算法可扩展）
        # 考虑朝向后的等效尺寸
        effective_l1 = l1 / 2
        effective_w1 = w1 / 2
        effective_l2 = l2 / 2
        effective_w2 = w2 / 2

        # 找到两个矩形中心点之间的向量
        dx = x2 - x1
        dy = y2 - y1

        # 使用分离轴定理计算最短距离
        # 简化为：计算点到矩形边的最短距离

        # 将障碍物中心转换到自车局部坐标系
        cos1 = math.cos(-yaw1)
        sin1 = math.sin(-yaw1)
        local_x = dx * cos1 - dy * sin1
        local_y = dx * sin1 + dy * cos1

        # 在自车局部坐标系中计算
        # 自车矩形范围: [-w1/2, w1/2] x [-l1/2, l1/2]
        half_w1 = w1 / 2
        half_l1 = l1 / 2
        half_w2 = w2 / 2
        half_l2 = l2 / 2

        # 障碍物在自车坐标系中的位置和朝向
        obs_local_x = local_x
        obs_local_y = local_y
        obs_local_yaw = yaw2 - yaw1

        # 计算最近点
        # 找到障碍物中心到自车矩形四条边的距离
        # 使用扩展的矩形距离公式

        # 将问题转化为：计算两个旋转矩形中心点之间的距离
        # 减去两个矩形在连线方向上的"半径"

        # 主轴方向
        axis_x = math.cos(obs_local_yaw)
        axis_y = math.sin(obs_local_yaw)

        # 在连线方向上的投影
        projection = obs_local_x * axis_x + obs_local_y * axis_y

        # 两个矩形在该方向上的半长度
        half_length1 = max(abs(math.cos(-yaw1) * half_l1) + abs(math.sin(-yaw1) * half_w1),
                         abs(math.cos(-yaw1 + math.pi/2) * half_l1) + abs(math.sin(-yaw1 + math.pi/2) * half_w1))
        half_length2 = max(abs(math.cos(obs_local_yaw) * half_l2) + abs(math.sin(obs_local_yaw) * half_w2),
                         abs(math.cos(obs_local_yaw + math.pi/2) * half_l2) + abs(math.sin(obs_local_yaw + math.pi/2) * half_w2))

        # 中心点距离
        center_dist = math.sqrt(dx * dx + dy * dy)

        # 最短距离 = 中心距离 - 两个矩形在该方向上的"半径"
        min_dist = center_dist - half_length1 - half_length2

        # 矩形可能重叠或包含
        if min_dist < 0:
            min_dist = 0

        return min_dist

    def calc_alarm_level(self, distance, direction):
        """计算报警等级"""
        if distance > self.alarm_thresholds['max']:
            return 'none', 'none'
        elif distance > self.alarm_thresholds['medium']:
            return 'warning', 'low'
        elif distance > self.alarm_thresholds['high']:
            return 'caution', 'medium'
        else:
            return 'danger', 'high'

    def get_alarm_text(self, distance, direction, obs_type):
        """生成报警文字"""
        if direction == 'far':
            return ''

        dir_text = {
            'left': '左侧',
            'left_front': '左前方',
            'front': '正前方',
            'right_front': '右前方',
            'right': '右侧'
        }

        type_text = {
            'truck': '卡车',
            'pedestrian': '行人',
            'other': '障碍物'
        }

        if distance < 3:
            dist_desc = '接近中'
        else:
            dist_desc = f'{distance:.1f}米'

        return f"{dir_text.get(direction, '')}{type_text.get(obs_type, '障碍物')}{dist_desc}"

    # 5 direction × 2 distance thresholds × 3 type = 30 combinations
    VALID_DIRECTIONS = {'left', 'left_front', 'front', 'right_front', 'right'}

    def _check_voice_trigger(self, track_id, current_priority, dir_val):
        """検查是否需要触发语音报警。
        触发条件：
          1. 优先级提升（主动上升）
          2. 同优先级但超过冷却时间（障碍物持续在场）
        返回: (voice_triggered: bool)
        """
        now = time.time()
        history = self.voice_alarm_history.get(track_id)
        last_priority = history['priority'] if history else 0
        last_trigger_time = history['last_trigger_time'] if history else 0.0

        voice_triggered = False

        if dir_val is None or current_priority == 0:
            # 无效方位或优先级为0，不触发
            pass
        elif current_priority > last_priority:
            # 优先级提升 → 立即触发
            voice_triggered = True
        elif current_priority == last_priority and (now - last_trigger_time) >= self.voice_alarm_cooldown:
            # 同优先级但冷却期已过 → 重新触发
            voice_triggered = True

        # 更新历史记录
        if voice_triggered:
            self.voice_alarm_history[track_id] = {'priority': current_priority, 'last_trigger_time': now}
        elif current_priority < last_priority:
            # 优先级下降（障碍物远离），重置历史使能重新触发
            self.voice_alarm_history[track_id] = {'priority': current_priority, 'last_trigger_time': 0.0}
        elif track_id not in self.voice_alarm_history:
            self.voice_alarm_history[track_id] = {'priority': current_priority, 'last_trigger_time': 0.0}

        return voice_triggered

    def calc_voice_alarm(self, distance, direction, obs_type):
        """Compute voice alarm, return split fields for downstream.
        Returns: (priority, direction, distance, type) or (0, None, None, None)
        direction: left|left_front|front|right_front|right
        distance: numeric (meters)
        type: truck|pedestrian|other
        """
        if distance > 10.0:
            return 0, None, None, None
        if direction not in self.VALID_DIRECTIONS:
            return 0, None, None, None

        if distance > 5.0:
            return 1, direction, round(distance, 2), obs_type
        elif distance > 1.5:
            return 2, direction, round(distance, 2), obs_type
        else:
            return 3, direction, round(distance, 2), obs_type

    def obstacle_callback(self, msg):
        """处理障碍物检测消息"""
        if not msg.markers:
            return

        self.logger.info(f"收到障碍物消息，包含 {len(msg.markers)} 个Marker")
        self.logger.debug(f"[TRACK] 当前跟踪列表: {list(self.tracked_obstacles.keys())}")

        # 打印所有 marker 的信息
        for i, m in enumerate(msg.markers):
            label = getattr(m, 'label', None)
            self.logger.info(f"[RAW] marker[{i}]: id={m.id}, label={label}, ns={m.ns}, pos=({m.pose.position.x:.1f}, {m.pose.position.y:.1f}), scale=({m.scale.x:.1f}x{m.scale.y:.1f}x{m.scale.z:.1f})")

        obstacles = []
        alarms = []
        highest_priority = 0

        # 初始化缓存
        if not hasattr(self, 'truck_cache'):
            self.truck_cache = {'cabs': {}, 'trailers': {}}

        now = rospy.Time.now()

        # 清理过期缓存（超过0.3秒）
        expired_time = 0.3
        for key in list(self.truck_cache['cabs'].keys()):
            if (now - self.truck_cache['cabs'][key]['timestamp']).to_sec() > expired_time:
                del self.truck_cache['cabs'][key]
        for key in list(self.truck_cache['trailers'].keys()):
            if (now - self.truck_cache['trailers'][key]['timestamp']).to_sec() > expired_time:
                del self.truck_cache['trailers'][key]

        # 记录已合并的 marker
        merged_ids = set()

        # 保存合并后的结果
        merged_trucks = []

        # 处理卡车（label=1,2,3）- 合并车头和车挂
        for marker in msg.markers:
            label = getattr(marker, 'label', 0) if hasattr(marker, 'label') else 0
            if label == 0 or label not in [1, 2, 3]:
                continue

            marker_id = (marker.id, label)
            if marker_id in merged_ids:
                continue

            track_id = self.get_track_id(marker)
            is_cab = (label == 1)

            if is_cab:
                # 车头：保存到缓存，检查是否与车挂重叠
                self.truck_cache['cabs'][track_id] = {
                    'marker': marker,
                    'timestamp': now
                }

                # 检查是否与已有的车挂重叠
                self.logger.info(f"[MERGE] 车头 id={marker.id}, scale=({marker.scale.x:.1f}x{marker.scale.y:.1f}x{marker.scale.z:.1f}), pos=({marker.pose.position.x:.1f}, {marker.pose.position.y:.1f})")
                self.logger.info(f"[MERGE] 当前缓存 cabs: {list(self.truck_cache['cabs'].keys())}, trailers: {list(self.truck_cache.get('trailers', {}).keys())}")

                for trail_id, trail_data in list(self.truck_cache.get('trailers', {}).items()):
                    trailer = trail_data['marker']
                    trail_label = getattr(trailer, 'label', 8)
                    dist = math.sqrt((marker.pose.position.x - trailer.pose.position.x)**2 +
                                    (marker.pose.position.y - trailer.pose.position.y)**2)
                    half_len1 = marker.scale.x / 2
                    half_len2 = trailer.scale.x / 2
                    threshold = half_len1 + half_len2 - 8
                    self.logger.info(f"[MERGE] 检查: cab(id={marker.id}) vs trailer(id={trailer.id}), 距离={dist:.2f}m, threshold={threshold:.2f}m, 重叠={dist < threshold}")
                    if self.boxes_overlap(marker, trailer):
                        # 重叠，合并
                        merged = self.do_merge(marker, trailer)
                        merged_id = (trailer.id, trail_label)
                        merged_ids.add(marker_id)
                        merged_ids.add(merged_id)
                        merged_trucks.append(merged)
                        del self.truck_cache['trailers'][trail_id]
                        del self.truck_cache['cabs'][track_id]
                        self.logger.info(f"[MERGE] 合并成功: cab(id={marker.id}) + trailer(id={trailer.id}), 距离={dist:.2f}m, 合并尺寸=({merged.scale.x:.1f}x{merged.scale.y:.1f}x{merged.scale.z:.1f})")
                        break

            else:
                # 车挂：检查是否与车头重叠
                self.logger.info(f"[MERGE] 车挂 id={marker.id}, scale=({marker.scale.x:.1f}x{marker.scale.y:.1f}x{marker.scale.z:.1f}), pos=({marker.pose.position.x:.1f}, {marker.pose.position.y:.1f})")
                self.logger.info(f"[MERGE] 当前缓存 cabs: {list(self.truck_cache['cabs'].keys())}")

                for cab_id, cab_data in list(self.truck_cache['cabs'].items()):
                    cab = cab_data['marker']
                    cab_label = getattr(cab, 'label', 8)
                    dist = math.sqrt((cab.pose.position.x - marker.pose.position.x)**2 +
                                    (cab.pose.position.y - marker.pose.position.y)**2)
                    half_len1 = cab.scale.x / 2
                    half_len2 = marker.scale.x / 2
                    threshold = half_len1 + half_len2 - 8
                    self.logger.info(f"[MERGE] 检查: cab(id={cab.id}) vs trailer(id={marker.id}), 距离={dist:.2f}m, threshold={threshold:.2f}m, 重叠={dist < threshold}")
                    if self.boxes_overlap(cab, marker):
                        # 重叠，合并
                        merged = self.do_merge(cab, marker)
                        merged_id = (cab.id, cab_label)
                        merged_ids.add(merged_id)
                        merged_ids.add(marker_id)
                        merged_trucks.append(merged)
                        del self.truck_cache['cabs'][cab_id]
                        self.logger.info(f"[MERGE] 合并成功: cab(id={cab.id}) + trailer(id={marker.id}), 距离={dist:.2f}m, 合并尺寸=({merged.scale.x:.1f}x{merged.scale.y:.1f}x{merged.scale.z:.1f})")
                        break
                else:
                    # 不重叠，缓存车挂供后续车头匹配（解决车头先于车挂出现的顺序依赖）
                    self.truck_cache.setdefault('trailers', {})[track_id] = {
                        'marker': marker,
                        'timestamp': now
                    }

        # 处理所有障碍物，跳过已合并的卡车
        for marker in msg.markers:
            label = getattr(marker, 'label', 0) if hasattr(marker, 'label') else 0
            marker_id = (marker.id, label)

            # 调试日志
            self.logger.debug(f"[DEBUG] 处理 marker: id={marker.id}, label={label}, in_merged_ids={marker_id in merged_ids}")

            # label=0 过滤掉
            if label == 0:
                self.logger.debug(f"[DEBUG] skip label=0, marker.id={marker.id}")
                continue

            # 检查是否是需要合并的卡车且已被合并
            marker_id = (marker.id, label)
            if label in [1, 2, 3] and marker_id in merged_ids:
                continue

            # 跳过不关心的 label
            # label: 1=cab, 2/3=trailer, 6=pedestrian, 8/9=other
            if label not in [1, 2, 3, 6, 8, 9]:
                self.logger.debug(f"[DEBUG] skip not关心的 label={label}, marker.id={marker.id}")
                continue

            obs_type = self.classify_obstacle(label)
            self.logger.debug(f"[DEBUG] 处理: marker.id={marker.id}, label={label}, type={obs_type}")

            # 位置（车体坐标系）
            vx = marker.pose.position.x
            vy = marker.pose.position.y
            vz = marker.pose.position.z

            # 尺寸
            length = marker.scale.x if hasattr(marker, 'scale') else 4.0
            width = marker.scale.y if hasattr(marker, 'scale') else 2.5
            height = marker.scale.z if hasattr(marker, 'scale') else 6.0

            # 朝向
            yaw = self.quaternion_to_yaw(
                marker.pose.orientation.x, marker.pose.orientation.y,
                marker.pose.orientation.z, marker.pose.orientation.w
            )

            # 计算距离和方位
            distance, direction = self.calc_distance_direction(marker)

            # 先获取track_id用于语音报警状态跟踪
            track_id = self.track_obstacle(obs_type, vx, vy)

            # 计算语音报警
            current_voice_priority, dir_val, dist_val, type_val = self.calc_voice_alarm(distance, direction, obs_type)

            # 检查是否需要触发报警（优先级提升 或 冷却期已过）
            voice_triggered = self._check_voice_trigger(track_id, current_voice_priority, dir_val)

            # 计算报警等级
            level, priority = self.calc_alarm_level(distance, direction)

            # 生成报警文字
            alarm_text = self.get_alarm_text(distance, direction, obs_type)

            # 障碍物数据（使用原始车体坐标系）
            obstacle = {
                'id': f"{obs_type}_{track_id}",
                'type': obs_type,
                'x': round(vx, 2),
                'y': round(vy, 2),
                'z': round(vz, 2),
                'length': round(length, 2),
                'width': round(width, 2),
                'height': round(height, 2),
                'rotationY': round(yaw, 1),
                'distance': round(distance, 2),
                'direction': direction
            }
            obstacles.append(obstacle)

            # 记录障碍物
            self.logger.info(f"障碍物: id={obstacle['id']}, type={obs_type}, pos=({vx:.2f}, {vy:.2f}, {vz:.2f}), 距离={distance:.1f}m, 方位={direction}, 报警={level}")

            # 报警信息
            if level != 'none':
                alarm = {
                    'level': level,
                    'text': alarm_text,
                    'priority': priority,
                    'distance': round(distance, 2),
                    'direction': direction,
                    'type': obs_type
                }
                if voice_triggered:
                    alarm['voice_triggered'] = True
                    alarm['voice_priority'] = current_voice_priority
                    alarm['track_id'] = track_id
                alarms.append(alarm)

                if priority == 'high':
                    highest_priority = max(highest_priority, 3)
                elif priority == 'medium':
                    highest_priority = max(highest_priority, 2)
                elif priority == 'low':
                    highest_priority = max(highest_priority, 1)

        # 处理合并后的卡车
        if merged_trucks:
            self.logger.info(f"[MERGE] 处理 {len(merged_trucks)} 个合并的卡车")
            for marker in merged_trucks:
                vx = marker.pose.position.x
                vy = marker.pose.position.y
                vz = marker.pose.position.z

                length = marker.scale.x
                width = marker.scale.y
                height = marker.scale.z

                yaw = self.quaternion_to_yaw(
                    marker.pose.orientation.x, marker.pose.orientation.y,
                    marker.pose.orientation.z, marker.pose.orientation.w
                )

                distance, direction = self.calc_distance_direction(marker)

                # 先获取track_id用于语音报警状态跟踪
                track_id = self.track_obstacle('truck', vx, vy)

                # 计算语音报警（合并卡车的类型是'truck'）
                current_voice_priority, dir_val, dist_val, type_val = self.calc_voice_alarm(distance, direction, 'truck')

                # 检查是否需要触发报警（优先级提升 或 冷却期已过）
                voice_triggered = self._check_voice_trigger(track_id, current_voice_priority, dir_val)

                level, priority = self.calc_alarm_level(distance, direction)
                alarm_text = self.get_alarm_text(distance, direction, 'truck')

                obstacle = {
                    'id': f"truck_{track_id}",
                    'type': 'truck',
                    'x': round(vx, 2),
                    'y': round(vy, 2),
                    'z': round(vz, 2),
                    'length': round(length, 2),
                    'width': round(width, 2),
                    'height': round(height, 2),
                    'rotationY': round(yaw, 1),
                    'distance': round(distance, 2),
                    'direction': direction
                }
                obstacles.append(obstacle)
                self.logger.info(f"[MERGE] 合并卡车: id=truck_{track_id}, pos=({vx:.2f}, {vy:.2f}, {vz:.2f}), size=({length:.1f}x{width:.1f}x{height:.1f})")

                if level != 'none':
                    alarm = {
                        'level': level,
                        'text': alarm_text,
                        'priority': priority,
                        'distance': round(distance, 2),
                        'direction': direction,
                        'type': 'truck'
                    }
                    if voice_triggered:
                        alarm['voice_triggered'] = True
                        alarm['voice_priority'] = current_voice_priority
                        alarm['track_id'] = track_id
                    alarms.append(alarm)
                    if priority == 'high':
                        highest_priority = max(highest_priority, 3)
                    elif priority == 'medium':
                        highest_priority = max(highest_priority, 2)
                    elif priority == 'low':
                        highest_priority = max(highest_priority, 1)

        if obstacles:
            for obs in obstacles:
                self.logger.info(f"发送: id={obs['id']}, type={obs['type']}, pos=({obs['x']}, {obs['y']}, {obs['z']})")
            self.logger.info(f"发送数据: 障碍物={len(obstacles)}, 报警={highest_priority}")

        # 清理过期的语音报警历史记录（障碍物消失）
        current_time = time.time()
        expired_track_ids = []
        for track_id in self.voice_alarm_history.keys():
            if track_id not in self.tracked_obstacles:
                # 障碍物已不在跟踪列表中
                if track_id == self.current_voice_alarm['track_id']:
                    self.current_voice_alarm = {'track_id': None, 'priority': 0, 'direction': None, 'distance': None, 'type': None}
                expired_track_ids.append(track_id)
        for track_id in expired_track_ids:
            del self.voice_alarm_history[track_id]

        # 从 alarms 中直接提取 voice_alarm
        voice_alarm_event = None
        triggered = [a for a in alarms if a.get('voice_triggered')]
        if triggered:
            triggered.sort(key=lambda a: (-a['voice_priority'], a['distance']))
            best = triggered[0]
            self.current_voice_alarm = {
                'track_id': best['track_id'],
                'priority': best['voice_priority'],
                'direction': best['direction'],
                'distance': best['distance'],
                'type': best['type']
            }
            voice_alarm_event = {k: best[k] for k in ('direction', 'distance', 'type')}
            voice_alarm_event['priority'] = best['voice_priority']
            self.logger.info(f"触发新语音报警: {voice_alarm_event}")

        data = {
            'timestamp': time.time(),
            'obstacles': obstacles,
            'alarms': alarms,
            'highest_priority': highest_priority
        }
        if voice_alarm_event is not None:
            data['voice_alarm'] = voice_alarm_event

        self._log_alarm_info(alarms, highest_priority, voice_alarm_event)
        self.send_data(data)
        self.publish_visualization(obstacles)

    def publish_visualization(self, obstacles):
        """发布障碍物可视化 MarkerArray"""
        if not obstacles:
            return

        marker_array = MarkerArray()
        timestamp = rospy.Time.now()

        for i, obs in enumerate(obstacles):
            marker = Marker()
            marker.header.stamp = timestamp
            marker.header.frame_id = 'base_link'
            marker.ns = obs.get('type', 'obstacle')
            marker.id = i
            marker.type = Marker.CUBE
            marker.action = Marker.ADD

            # 位置：车体坐标系(x右,y前,z上) -> base_link(x前,y左,z上)，即 x'=y, y'=-x, z'=z
            vx, vy, vz = obs.get('x', 0), obs.get('y', 0), obs.get('z', 0)
            marker.pose.position.x = vy   # 前
            marker.pose.position.y = -vx  # 左
            marker.pose.position.z = vz   # 上

            # 旋转
            qx, qy, qz, qw = self.euler_to_quaternion(0, 0, math.radians(obs.get('rotationY', 0)))
            marker.pose.orientation.x = qx
            marker.pose.orientation.y = qy
            marker.pose.orientation.z = qz
            marker.pose.orientation.w = qw

            # 尺寸
            marker.scale.x = obs.get('length', 2)
            marker.scale.y = obs.get('width', 2)
            marker.scale.z = obs.get('height', 2)

            # 颜色
            obs_type = obs.get('type', 'other')
            if obs_type == 'truck':
                marker.color.r = 0.5
                marker.color.g = 0.5
                marker.color.b = 0.5
            elif obs_type == 'pedestrian':
                marker.color.r = 0.0
                marker.color.g = 1.0
                marker.color.b = 0.0
            else:
                marker.color.r = 1.0
                marker.color.g = 0.5
                marker.color.b = 0.0
            marker.color.a = 0.5

            marker.lifetime = rospy.Duration(0.2)
            marker_array.markers.append(marker)

        self.visual_pub.publish(marker_array)

    def euler_to_quaternion(self, roll, pitch, yaw):
        """欧拉角转四元数"""
        cr = math.cos(roll / 2)
        sr = math.sin(roll / 2)
        cp = math.cos(pitch / 2)
        sp = math.sin(pitch / 2)
        cy = math.cos(yaw / 2)
        sy = math.sin(yaw / 2)

        qw = cr * cp * cy + sr * sp * sy
        qx = sr * cp * cy - cr * sp * sy
        qy = cr * sp * cy + sr * cp * sy
        qz = cr * cp * sy - sr * sp * cy

        return (qx, qy, qz, qw)


def main():
    rospy.init_node('hmi_backend')
    backend = HMIBackend()
    rospy.spin()


if __name__ == '__main__':
    main()
