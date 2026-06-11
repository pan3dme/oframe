import 'package:fitness_app/Screen/line_chart.dart';
import 'package:fitness_app/Utils/colors.dart';
import 'package:flutter/material.dart';
import 'package:syncfusion_flutter_gauges/gauges.dart';

class FitnessHomeScreen extends StatefulWidget {
  FitnessHomeScreen({super.key});

  @override
  State<FitnessHomeScreen> createState() => _FitnessHomeScreenState();
}

class _FitnessHomeScreenState extends State<FitnessHomeScreen> {
  int currentIndex = 1;

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: ListView(
        children: [
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 20),
            child: Row(
              mainAxisAlignment: MainAxisAlignment.spaceBetween,
              children: [
                GestureDetector(
                  onTap: () {
                    Navigator.pop(context);
                  },
                  child: const Icon(
                    Icons.arrow_back_ios,
                    size: 20,
                  ),
                ),
                CircleAvatar(
                  backgroundColor: Color(0xFF4285F4),
                  radius: 20,
                  child: Icon(
                    Icons.person,
                    color: Colors.white,
                    size: 24,
                  ),
                ),
              ],
            ),
          ),
          Container(
            padding: const EdgeInsets.symmetric(
              horizontal: 20,
              vertical: 10,
            ),
            child: const Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  "Today",
                  style: TextStyle(
                    fontSize: 18,
                    color: Colors.black45,
                  ),
                ),
                Text(
                  "Sep 01, 2020",
                  style: TextStyle(
                      fontSize: 20,
                      color: Colors.black,
                      fontWeight: FontWeight.w800),
                )
              ],
            ),
          ),
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 20),
            child: Row(
              children: [
                fitnessItems(
                  0,
                  Colors.deepOrange,
                  Icons.favorite_border,
                  "Heart",
                  "80",
                  "Per min",
                ),
                fitnessItems(
                  1,
                  primaryColor,
                  Icons.crisis_alert_sharp,
                  "Calories",
                  "950",
                  "Kcal",
                ),
                fitnessItems(
                  2,
                  Colors.orangeAccent,
                  Icons.nightlight_round_outlined,
                  "Sleep",
                  "8:30",
                  "Hours",
                ),
                fitnessItems(
                  4,
                  Colors.deepPurple,
                  Icons.timer_sharp,
                  "Training",
                  "2:00",
                  "Hours",
                ),
              ],
            ),
          ),
          const SizedBox(height: 30),
          Center(
            child: Stack(
              children: [
                Positioned(
                  left: 80,
                  top: 50,
                  child: SizedBox(
                    height: 200,
                    width: 200,
                    child: SfRadialGauge(
                      axes: [
                        RadialAxis(
                          axisLineStyle: const AxisLineStyle(
                            thickness: 0.2,
                            thicknessUnit: GaugeSizeUnit.factor,
                            cornerStyle: CornerStyle.bothCurve,
                          ),
                          showTicks: false,
                          showLabels: false,
                          pointers: const [
                            RangePointer(
                              color: primaryColor,
                              value: 80,
                              cornerStyle: CornerStyle.bothCurve,
                              width: 0.2,
                              sizeUnit: GaugeSizeUnit.factor,
                            ),
                          ],
                          annotations: const [
                            GaugeAnnotation(
                              widget: Column(
                                mainAxisAlignment: MainAxisAlignment.center,
                                children: [
                                  Text(
                                    "2.0",
                                    style: TextStyle(
                                      fontWeight: FontWeight.w900,
                                      fontSize: 30,
                                    ),
                                  ),
                                  Text(
                                    "KM",
                                    style: TextStyle(
                                      fontSize: 14,
                                    ),
                                  )
                                ],
                              ),
                              positionFactor: 0.1,
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ),
                Padding(
                  padding: const EdgeInsets.only(left: 30),
                  child: SizedBox(
                    height: 300,
                    width: 300,
                    child: SfRadialGauge(
                      axes: [
                        RadialAxis(
                          axisLineStyle: const AxisLineStyle(
                            thickness: 0.15,
                            thicknessUnit: GaugeSizeUnit.factor,
                            cornerStyle: CornerStyle.bothCurve,
                          ),
                          showTicks: false,
                          showLabels: false,
                          pointers: const [
                            RangePointer(
                              color: secondaryColor,
                              value: 65,
                              cornerStyle: CornerStyle.bothCurve,
                              width: 0.15,
                              sizeUnit: GaugeSizeUnit.factor,
                            ),
                          ],
                        ),
                      ],
                    ),
                  ),
                ),
                Positioned(
                  bottom: 10,
                  left: 140,
                  child: Icon(
                    Icons.directions_run,
                    size: 100,
                    color: primaryColor.withOpacity(0.7),
                  ),
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),
          SizedBox(
            height: MediaQuery.of(context).size.height / 5,
            child: const Stack(
              clipBehavior: Clip.none,
              children: [
                MyLineChart(),
                Positioned(
                  top: -10,
                  left: 90,
                  child: CircleAvatar(
                    radius: 10,
                    backgroundColor: secondaryColor,
                  ),
                ),
                Positioned(
                    bottom: 40,
                    left: 15,
                    right: 15,
                    child: Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        Text(
                          "1km",
                          style: TextStyle(fontSize: 15),
                        ),
                        Text(
                          "2km",
                          style: TextStyle(fontSize: 15),
                        ),
                        Text(
                          "3km",
                          style: TextStyle(fontSize: 15),
                        ),
                        Text(
                          "4km",
                          style: TextStyle(fontSize: 15),
                        ),
                        Text(
                          "5km",
                          style: TextStyle(fontSize: 15),
                        ),
                        Text(
                          "6km",
                          style: TextStyle(fontSize: 15),
                        )
                      ],
                    ))
              ],
            ),
          ),
          const SizedBox(height: 20),
          // 快捷功能区
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 20, vertical: 15),
            margin: const EdgeInsets.symmetric(horizontal: 20),
            decoration: BoxDecoration(
              color: Colors.grey.shade100,
              borderRadius: BorderRadius.circular(20),
              border: Border.all(color: Colors.grey.shade300),
            ),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Row(
                  children: [
                    Icon(Icons.star, color: primaryColor, size: 20),
                    const SizedBox(width: 8),
                    const Text(
                      "快捷功能",
                      style: TextStyle(
                        fontSize: 18,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ],
                ),
                const SizedBox(height: 15),
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceAround,
                  children: [
                    _buildQuickAction(
                      Icons.devices,
                      "设备管理",
                      Colors.blue,
                    ),
                    _buildQuickAction(
                      Icons.pets,
                      "牛羊管理",
                      Colors.green,
                    ),
                    _buildQuickAction(
                      Icons.list_alt,
                      "功能列表",
                      Colors.orange,
                    ),
                    _buildQuickAction(
                      Icons.map,
                      "地图中心",
                      Colors.purple,
                    ),
                  ],
                ),
              ],
            ),
          ),
          const SizedBox(height: 20),
        ],
      ),
      bottomNavigationBar: const SizedBox(
        height: 90,
        child: BottomAppBar(
          color: primaryColor,
          child: Stack(
            clipBehavior: Clip.none,
            children: [
              Positioned(
                left: 180,
                top: 0,
                child: CircleAvatar(
                  radius: 25,
                  backgroundColor: secondaryColor,
                  child: Icon(
                    Icons.add,
                    color: Colors.white,
                  ),
                ),
              ),
              Padding(
                padding: EdgeInsets.only(top: 12, left: 15, right: 15),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.spaceBetween,
                  children: [
                    Icon(
                      Icons.house_outlined,
                      color: Colors.white,
                      size: 30,
                    ),
                    Icon(
                      Icons.line_axis_rounded,
                      color: Colors.white,
                      size: 30,
                    ),
                    SizedBox(width: 40),
                    Icon(
                      Icons.favorite_border,
                      color: Colors.white,
                      size: 30,
                    ),
                    Icon(
                      Icons.person_outline,
                      color: Colors.white,
                      size: 30,
                    ),
                  ],
                ),
              ),
            ],
          ),
        ),
      ),
    );
  }

  Expanded fitnessItems(int index, color, icon, name, value, unit) {
    return Expanded(
      child: Container(
        padding: const EdgeInsets.symmetric(vertical: 30),
        decoration: BoxDecoration(
          border: Border.all(
            color: currentIndex == index ? secondaryColor : Colors.transparent,
          ),
          borderRadius: BorderRadius.circular(50),
        ),
        child: Column(
          children: [
            Container(
              padding: const EdgeInsets.all(6),
              decoration: BoxDecoration(
                shape: BoxShape.circle,
                color: color,
              ),
              child: Icon(
                icon,
                color: Colors.white,
              ),
            ),
            const SizedBox(height: 10),
            Text(
              name,
              style: const TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.bold,
              ),
            ),
            Text(
              value,
              style: const TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.bold,
              ),
            ),
            Text(
              unit,
              style: const TextStyle(
                fontSize: 12,
                color: Colors.black38,
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildQuickAction(IconData icon, String label, Color color) {
    return InkWell(
      onTap: () {
        // TODO: 添加点击事件
        print('点击了 $label');
      },
      child: Column(
        mainAxisSize: MainAxisSize.min,
        children: [
          Container(
            width: 60,
            height: 60,
            decoration: BoxDecoration(
              color: color.withOpacity(0.1),
              borderRadius: BorderRadius.circular(15),
              border: Border.all(
                color: color.withOpacity(0.3),
                width: 1.5,
              ),
            ),
            child: Icon(
              icon,
              color: color,
              size: 30,
            ),
          ),
          const SizedBox(height: 8),
          Text(
            label,
            style: const TextStyle(
              fontSize: 12,
              fontWeight: FontWeight.w500,
            ),
          ),
        ],
      ),
    );
  }
}
