import 'package:fitness_app/Screen/fitness_home_screen.dart';
import 'package:fitness_app/Utils/colors.dart';
import 'package:flutter/material.dart';

class MySplashScreen extends StatelessWidget {
  const MySplashScreen({super.key});

  @override
  Widget build(BuildContext context) {
    Size size = MediaQuery.of(context).size;
    return Scaffold(
      body: Column(
        children: [
          const SizedBox(height: 40),
          Container(
            height: size.height * 0.63,
            width: size.width,
            decoration: BoxDecoration(
              gradient: LinearGradient(
                colors: [Color(0xFF4285F4), Color(0xFF34A853)],
                begin: Alignment.topLeft,
                end: Alignment.bottomRight,
              ),
            ),
            child: Center(
              child: Icon(
                Icons.fitness_center,
                size: 150,
                color: Colors.white,
              ),
            ),
          ),
          Padding(
            padding: const EdgeInsets.symmetric(horizontal: 20),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                SizedBox(
                  width: size.width * 0.75,
                  child: const Text(
                    "31 Days Fitness\nChallenge",
                    style: TextStyle(
                      fontSize: 30,
                      fontWeight: FontWeight.w900,
                    ),
                  ),
                ),
                const SizedBox(height: 20),
                const Text(
                  "Track your fitness level by our smart Mobile App, Calories sleep and training.",
                  style: TextStyle(
                    color: Colors.black54,
                    fontWeight: FontWeight.w500,
                  ),
                ),
                const SizedBox(height: 40),
                // 4个悬浮按钮
                Row(
                  mainAxisAlignment: MainAxisAlignment.spaceEvenly,
                  children: [
                    _buildFloatingButton(
                      context,
                      Icons.devices,
                      "设备管理",
                      Colors.blue,
                      () {
                        print('点击设备管理');
                      },
                    ),
                    _buildFloatingButton(
                      context,
                      Icons.pets,
                      "牛羊管理",
                      Colors.green,
                      () {
                        print('点击牛羊管理');
                      },
                    ),
                    _buildFloatingButton(
                      context,
                      Icons.list_alt,
                      "功能列表",
                      Colors.orange,
                      () {
                        print('点击功能列表');
                      },
                    ),
                    _buildFloatingButton(
                      context,
                      Icons.map,
                      "地图中心",
                      Colors.purple,
                      () {
                        Navigator.push(
                          context,
                          MaterialPageRoute(
                            builder: (context) => FitnessHomeScreen(),
                          ),
                        );
                      },
                    ),
                  ],
                )
              ],
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildFloatingButton(
    BuildContext context,
    IconData icon,
    String label,
    Color color,
    VoidCallback onTap,
  ) {
    return Column(
      mainAxisSize: MainAxisSize.min,
      children: [
        InkWell(
          onTap: onTap,
          child: Material(
            elevation: 8,
            shadowColor: color.withOpacity(0.4),
            borderRadius: BorderRadius.circular(20),
            child: Container(
              width: 70,
              height: 70,
              decoration: BoxDecoration(
                gradient: LinearGradient(
                  colors: [color, color.withOpacity(0.7)],
                  begin: Alignment.topLeft,
                  end: Alignment.bottomRight,
                ),
                borderRadius: BorderRadius.circular(20),
                boxShadow: [
                  BoxShadow(
                    color: color.withOpacity(0.3),
                    blurRadius: 10,
                    offset: const Offset(0, 5),
                  ),
                ],
              ),
              child: Icon(
                icon,
                color: Colors.white,
                size: 35,
              ),
            ),
          ),
        ),
        const SizedBox(height: 10),
        Text(
          label,
          style: const TextStyle(
            fontSize: 12,
            fontWeight: FontWeight.w600,
            color: Colors.black87,
          ),
        ),
      ],
    );
  }
}
