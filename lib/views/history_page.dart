// history_page.dart — Riwayat dan tren
// Grafik metrik, daftar log, ekspor CSV.
import 'package:flutter/material.dart';
import 'package:provider/provider.dart';
import '../models/sensor_model.dart';
import '../providers/history_provider.dart';
import '../providers/sensor_provider.dart';
import '../theme/app_theme.dart';
import '../widgets/app_toast.dart';
import '../widgets/botanic_vector_decor.dart';
import '../widgets/sensor_trend_chart.dart';
import '../widgets/shimmer_loading.dart';

// Grafik tren di atas, daftar log di bawah, ekspor CSV lewat tombol melayang.
// Sumber data: HistoryProvider (maks 50 entri) + ambang dari SensorProvider.
class HistoryPage extends StatefulWidget {
  const HistoryPage({super.key});

  @override
  State<HistoryPage> createState() => _HistoryPageState();
}

class _HistoryPageState extends State<HistoryPage> {
  String? _selectedDeviceId;

  @override
  // Memuat log saat halaman pertama dibuka. Bila provider sudah punya data,
  // request tidak diulang agar perpindahan tab terasa instan.
  void initState() {
    super.initState();
    WidgetsBinding.instance.addPostFrameCallback((_) {
      final provider = Provider.of<HistoryProvider>(context, listen: false);
      if (provider.historyList.isEmpty || provider.errorMessage != null) {
        provider.fetchHistory();
      }
    });
  }

  @override
  Widget build(BuildContext context) {
    return Consumer<HistoryProvider>(
      builder: (context, provider, child) {
        return Scaffold(
          backgroundColor: AppColors.background,
          body: RefreshIndicator(
            onRefresh: () => provider.fetchHistory(),
            child: _buildBody(context, provider),
          ),
          floatingActionButton: SizedBox(
            height: 38,
            child: FloatingActionButton.extended(
              onPressed: provider.historyForDevice(_selectedDeviceId).isEmpty
                  ? null
                  : () async {
                      try {
                        final path = await provider.exportToCsv(
                          deviceId: _selectedDeviceId,
                        );
                        if (path != null && context.mounted) {
                          AppToast.success(context, 'Berhasil diexport ke:\n$path');
                        }
                      } catch (e) {
                        if (context.mounted) {
                          AppToast.error(context, e.toString());
                        }
                      }
                    },
              elevation: 2,
              highlightElevation: 3,
              backgroundColor: AppColors.pharmaNavy,
              icon: const Icon(Icons.file_download_outlined, color: Colors.white, size: 16),
              label: const Text(
                'Export CSV',
                style: TextStyle(
                  color: Colors.white,
                  fontWeight: FontWeight.w700,
                  fontSize: 12,
                  letterSpacing: 0.2,
                ),
              ),
              shape: RoundedRectangleBorder(
                borderRadius: BorderRadius.circular(AppSpacing.radiusSm),
              ),
            ),
          ),
        );
      },
    );
  }

  // Tiga keadaan: skeleton saat memuat, pesan error dengan tombol coba lagi,
  // atau daftar log. Pull-to-refresh memanggil fetchHistory().
  Widget _buildBody(BuildContext context, HistoryProvider provider) {
    return Consumer<SensorProvider>(
      builder: (context, sensorProvider, child) {
        if (provider.isLoading) {
          return _buildLoadingSkeleton();
        }

        if (provider.errorMessage != null) {
          return Center(
            child: Padding(
              padding: const EdgeInsets.all(AppSpacing.lg),
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  const Icon(
                    Icons.error_outline,
                    size: 64,
                    color: Colors.redAccent,
                  ),
                  const SizedBox(height: AppSpacing.md),
                  Text(
                    provider.errorMessage!,
                    textAlign: TextAlign.center,
                    style: AppTypography.body,
                  ),
                  const SizedBox(height: AppSpacing.md),
                  if (provider.errorMessage?.contains('login') == true ||
                      provider.errorMessage?.contains('Sesi') == true) ...[
                    ElevatedButton.icon(
                      style: ElevatedButton.styleFrom(
                        backgroundColor: AppColors.pharmaNavy,
                        foregroundColor: Colors.white,
                        padding: const EdgeInsets.symmetric(horizontal: AppSpacing.lg, vertical: AppSpacing.md - 4),
                        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(AppSpacing.radiusMd)),
                      ),
                      onPressed: () {
                        Navigator.pushReplacementNamed(context, '/login');
                      },
                      icon: const Icon(Icons.login_rounded, size: 18),
                      label: const Text(
                        'Login Ulang',
                        style: TextStyle(
                          fontWeight: FontWeight.bold,
                          height: 1.1,
                          leadingDistribution: TextLeadingDistribution.even,
                        ),
                      ),
                    ),
                  ] else ...[
                    ElevatedButton.icon(
                      style: ElevatedButton.styleFrom(
                        backgroundColor: AppColors.pharmaNavy,
                        foregroundColor: Colors.white,
                        padding: const EdgeInsets.symmetric(horizontal: AppSpacing.lg, vertical: AppSpacing.md - 4),
                        shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(AppSpacing.radiusMd)),
                      ),
                      onPressed: () => provider.fetchHistory(),
                      icon: const Icon(Icons.refresh, size: 18),
                      label: const Text(
                        'Coba Lagi',
                        style: TextStyle(
                          fontWeight: FontWeight.bold,
                          height: 1.1,
                          leadingDistribution: TextLeadingDistribution.even,
                        ),
                      ),
                    ),
                  ],
                ],
              ),
            ),
          );
        }

        if (provider.historyList.isEmpty) {
          return Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                const Icon(
                  Icons.inbox_outlined,
                  size: 64,
                  color: AppColors.textMuted,
                ),
                const SizedBox(height: AppSpacing.md),
                const Text(
                  'Belum ada data log gudang farmasi.',
                  style: TextStyle(fontSize: 16, color: AppColors.textMuted),
                ),
                const SizedBox(height: AppSpacing.md),
                ElevatedButton.icon(
                  style: ElevatedButton.styleFrom(
                    backgroundColor: AppColors.pharmaNavy,
                    foregroundColor: Colors.white,
                    padding: const EdgeInsets.symmetric(horizontal: AppSpacing.lg, vertical: AppSpacing.md - 4),
                    shape: RoundedRectangleBorder(borderRadius: BorderRadius.circular(AppSpacing.radiusMd)),
                  ),
                  onPressed: () => provider.fetchHistory(),
                  icon: const Icon(Icons.refresh, size: 18),
                  label: const Text(
                    'Muat Ulang',
                    style: TextStyle(
                      fontWeight: FontWeight.bold,
                      height: 1.1,
                      leadingDistribution: TextLeadingDistribution.even,
                    ),
                  ),
                ),
              ],
            ),
          );
        }

        final displayList = provider.historyForDevice(_selectedDeviceId);
        final bool isSterilFilter = _selectedDeviceId == 'device_2';
        final double chartTempMin = isSterilFilter ? 15.0 : sensorProvider.tempMin;
        final double chartTempMax = isSterilFilter ? 25.0 : sensorProvider.tempMax;
        final double chartHumMin = isSterilFilter ? 45.0 : sensorProvider.humidityMin;
        final double chartHumMax = isSterilFilter ? 60.0 : sensorProvider.humidityMax;

        final double avgSuhu = displayList.isEmpty
            ? 0.0
            : displayList.map((e) => e.suhu).reduce((a, b) => a + b) / displayList.length;
        final double avgGas = displayList.isEmpty
            ? 0.0
            : displayList.map((e) => e.kadarGas).reduce((a, b) => a + b) / displayList.length;

        return ListView(
          physics: const BouncingScrollPhysics(),
          padding: const EdgeInsets.only(
            left: AppSpacing.md,
            right: AppSpacing.md,
            top: AppSpacing.sm + 4,
            bottom: 84,
          ),
          children: [
            _buildZoneSelector(),
            const SizedBox(height: AppSpacing.md),
            if (displayList.isEmpty)
              Container(
                padding: const EdgeInsets.symmetric(vertical: 48, horizontal: AppSpacing.md),
                decoration: BoxDecoration(
                  color: AppColors.cardWhite,
                  borderRadius: BorderRadius.circular(AppSpacing.radiusMd),
                  border: Border.all(color: AppColors.containerLow),
                ),
                child: const Column(
                  children: [
                    Icon(Icons.inbox_outlined, size: 42, color: AppColors.textMuted),
                    SizedBox(height: AppSpacing.sm),
                    Text(
                      'Belum ada log untuk zona ini pada 200 riwayat terakhir.',
                      textAlign: TextAlign.center,
                      style: TextStyle(fontSize: 13, color: AppColors.textMuted),
                    ),
                  ],
                ),
              )
            else ...[
              Row(
                children: [
                  Expanded(
                    child: _buildMetricPillCard(
                      icon: Icons.list_alt_rounded,
                      title: 'Total Log',
                      value: '${displayList.length}',
                      color: AppColors.pharmaNavy,
                    ),
                  ),
                  const SizedBox(width: AppSpacing.sm),
                  Expanded(
                    child: _buildMetricPillCard(
                      icon: Icons.thermostat_rounded,
                      title: 'Rata Suhu',
                      value: '${avgSuhu.toStringAsFixed(1)} °C',
                      color: AppColors.tempRed,
                    ),
                  ),
                  const SizedBox(width: AppSpacing.sm),
                  Expanded(
                    child: _buildMetricPillCard(
                      icon: Icons.cloud_outlined,
                      title: 'Rata Gas',
                      value: '${avgGas.toStringAsFixed(1)} PPM',
                      color: AppColors.warningAmber,
                    ),
                  ),
                ],
              ),
              const SizedBox(height: AppSpacing.md),
              BotanicVectorCardWatermark(
                size: 140,
                opacity: 0.035,
                alignment: Alignment.topRight,
                child: SensorTrendChart(
                  historyData: displayList,
                  tempMin: chartTempMin,
                  tempMax: chartTempMax,
                  humidityMin: chartHumMin,
                  humidityMax: chartHumMax,
                  gasMax: sensorProvider.gasMax,
                  isVaccineRack: !isSterilFilter,
                  isMultiZone: _selectedDeviceId == null,
                ),
              ),
              const SizedBox(height: AppSpacing.md + 2),
              Row(
                children: [
                  const Text(
                    'Catatan Terkini',
                    style: AppTypography.subtitle,
                  ),
                  const Spacer(),
                  Container(
                    alignment: Alignment.center,
                    padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 4),
                    decoration: BoxDecoration(
                      color: AppColors.pharmaNavy.withAlpha(20),
                      borderRadius: BorderRadius.circular(AppSpacing.radiusSm),
                    ),
                    child: Text(
                      '${displayList.length} Catatan',
                      textAlign: TextAlign.center,
                      style: AppTypography.badge.copyWith(
                        color: AppColors.pharmaNavy,
                      ),
                    ),
                  ),
                ],
              ),
              const SizedBox(height: AppSpacing.sm),
              ListView.builder(
                shrinkWrap: true,
                physics: const NeverScrollableScrollPhysics(),
                itemCount: displayList.length,
                itemBuilder: (context, index) {
                  final item = displayList[index];
                  final logNumber = displayList.length - index;
                  return _buildLogCard(item, logNumber, sensorProvider);
                },
              ),
            ],
          ],
        );
      },
    );
  }

  Widget _buildZoneSelector() {
    return Container(
      padding: const EdgeInsets.all(4),
      decoration: BoxDecoration(
        color: AppColors.cardWhite,
        borderRadius: BorderRadius.circular(AppSpacing.radiusMd),
        border: Border.all(color: AppColors.containerLow, width: 1.0),
        boxShadow: const [
          BoxShadow(
            color: Color(0x0C0E555A),
            blurRadius: 10,
            offset: Offset(0, 3),
          ),
        ],
      ),
      child: Row(
        children: [
          _buildZoneTab('Semua', null),
          _buildZoneTab('Rak Vaksin', 'device_1'),
          _buildZoneTab('Ruang Steril', 'device_2'),
        ],
      ),
    );
  }

  Widget _buildZoneTab(String label, String? deviceId) {
    final isSelected = _selectedDeviceId == deviceId;
    return Expanded(
      child: InkWell(
        onTap: () => setState(() => _selectedDeviceId = deviceId),
        borderRadius: BorderRadius.circular(AppSpacing.radiusSm),
        child: AnimatedContainer(
          duration: const Duration(milliseconds: 180),
          height: 40,
          alignment: Alignment.center,
          decoration: BoxDecoration(
            color: isSelected ? AppColors.pharmaNavy : Colors.transparent,
            borderRadius: BorderRadius.circular(AppSpacing.radiusSm),
          ),
          child: Text(
            label,
            style: TextStyle(
              fontSize: 12.5,
              fontWeight: FontWeight.w800,
              color: isSelected ? Colors.white : AppColors.textMuted,
            ),
          ),
        ),
      ),
    );
  }

  // Satu kartu log. Nilai di luar ambang diberi warna peringatan; ambang suhu
  // mengikuti node log itu sendiri (Rak Vaksin 2-8 C atau Ruang Steril 15-25 C).
  Widget _buildLogCard(SensorModel item, int logNumber, SensorProvider sensorProvider) {
    final bool isSteril = item.deviceId == 'device_2';
    final double minT = isSteril ? 15.0 : sensorProvider.tempMin;
    final double maxT = isSteril ? 25.0 : sensorProvider.tempMax;
    final bool tempAlert = item.suhu > maxT || item.suhu < minT;
    final bool gasAlert = item.kadarGas > sensorProvider.gasMax;
    final bool doorAlert = item.isPintuTerbuka;
    final bool isWarning = tempAlert || gasAlert || doorAlert;

    // Format cleaner time: only HH:mm:ss if today
    String displayTime = item.waktu ?? 'N/A';
    final parts = displayTime.split(' ');
    if (parts.length > 1) {
      displayTime = parts[1]; // show HH:mm:ss
    }

    return Container(
      margin: const EdgeInsets.only(bottom: 9),
      padding: const EdgeInsets.all(AppSpacing.md),
      decoration: BoxDecoration(
        color: AppColors.cardWhite,
        borderRadius: BorderRadius.circular(AppSpacing.radiusMd),
        border: Border.all(
          color: isWarning ? AppColors.alertRed.withAlpha(80) : AppColors.containerLow,
          width: 1.0,
        ),
        boxShadow: const [
          BoxShadow(
            color: Color(0x0C0E555A),
            blurRadius: 8,
            offset: Offset(0, 2),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Text(
                'Log $logNumber',
                style: const TextStyle(
                  fontSize: 14,
                  fontWeight: FontWeight.w800,
                  color: AppColors.pharmaNavy,
                ),
              ),
              const SizedBox(width: 8),
              Text(
                displayTime,
                style: const TextStyle(
                  fontSize: 12,
                  color: AppColors.textMuted,
                  fontWeight: FontWeight.w500,
                ),
              ),
              if (_selectedDeviceId == null) ...[
                const SizedBox(width: 6),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                  decoration: BoxDecoration(
                    color: AppColors.inputBg,
                    borderRadius: BorderRadius.circular(4),
                  ),
                  child: Text(
                    isSteril ? 'Steril' : 'Vaksin',
                    style: const TextStyle(
                      fontSize: 10,
                      fontWeight: FontWeight.w700,
                      color: AppColors.textMuted,
                    ),
                  ),
                ),
              ],
              const Spacer(),
              // Ikon status pintu & UV di baris atas
              Icon(
                doorAlert ? Icons.door_sliding_outlined : Icons.lock_outline,
                size: 15,
                color: doorAlert ? AppColors.alertRed : AppColors.activeGreen,
              ),
              const SizedBox(width: 6),
              Icon(
                Icons.cleaning_services_outlined,
                size: 15,
                color: item.isUvMenyala ? AppColors.uvPurple : AppColors.textMuted.withAlpha(120),
              ),
              const SizedBox(width: 8),
              Container(
                padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
                decoration: BoxDecoration(
                  color: (isWarning ? AppColors.alertRed : AppColors.activeGreen).withAlpha(20),
                  borderRadius: BorderRadius.circular(AppSpacing.radiusSm),
                ),
                child: Text(
                  isWarning ? 'Peringatan' : 'Aman',
                  style: TextStyle(
                    fontSize: 11,
                    fontWeight: FontWeight.w800,
                    color: isWarning ? AppColors.alertRed : AppColors.activeGreen,
                  ),
                ),
              ),
            ],
          ),
          const SizedBox(height: 10),
          Row(
            children: [
              // Suhu
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Suhu',
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w600,
                        color: AppColors.textMuted,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      '${item.suhu.toStringAsFixed(1)}°C',
                      style: TextStyle(
                        fontSize: 15.5,
                        fontWeight: FontWeight.w800,
                        color: tempAlert ? AppColors.alertRed : AppColors.pharmaNavy,
                      ),
                    ),
                  ],
                ),
              ),
              // Kelembapan
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Kelembapan',
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w600,
                        color: AppColors.textMuted,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      '${item.kelembapan.toStringAsFixed(0)}%',
                      style: const TextStyle(
                        fontSize: 15.5,
                        fontWeight: FontWeight.w800,
                        color: AppColors.textDark,
                      ),
                    ),
                  ],
                ),
              ),
              // Kadar Gas
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Kadar Gas',
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w600,
                        color: AppColors.textMuted,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      '${item.kadarGas.toStringAsFixed(0)} PPM',
                      style: TextStyle(
                        fontSize: 15.5,
                        fontWeight: FontWeight.w800,
                        color: gasAlert ? AppColors.alertRed : AppColors.textDark,
                      ),
                    ),
                  ],
                ),
              ),
              // Cahaya
              Expanded(
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Cahaya',
                      style: TextStyle(
                        fontSize: 11,
                        fontWeight: FontWeight.w600,
                        color: AppColors.textMuted,
                      ),
                    ),
                    const SizedBox(height: 2),
                    Text(
                      '${item.cahaya.toStringAsFixed(0)} Lx',
                      style: TextStyle(
                        fontSize: 15.5,
                        fontWeight: FontWeight.w800,
                        color: item.cahaya > 600
                            ? AppColors.alertRed
                            : (item.cahaya > 300 ? AppColors.warningAmber : AppColors.textDark),
                      ),
                    ),
                  ],
                ),
              ),
            ],
          ),
        ],
      ),
    );
  }

  // Ringkasan rata-rata suhu dan gas di atas daftar, membantu membaca tren
  // tanpa membuka setiap entri.
  Widget _buildMetricPillCard({
    required IconData icon,
    required String title,
    required String value,
    required Color color,
  }) {
    return Container(
      padding: const EdgeInsets.symmetric(
        vertical: 12,
        horizontal: 10,
      ),
      decoration: BoxDecoration(
        color: AppColors.cardWhite,
        borderRadius: BorderRadius.circular(AppSpacing.radiusMd),
        border: Border.all(color: AppColors.containerLow, width: 1.0),
        boxShadow: const [
          BoxShadow(
            color: Color(0x0C0E555A),
            blurRadius: 10,
            offset: Offset(0, 3),
          ),
        ],
      ),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Row(
            children: [
              Container(
                padding: const EdgeInsets.all(5),
                decoration: BoxDecoration(
                  color: color.withAlpha(22),
                  shape: BoxShape.circle,
                ),
                child: Icon(icon, color: color, size: 14),
              ),
              const SizedBox(width: 6),
              Expanded(
                child: Text(
                  title,
                  style: const TextStyle(
                    fontSize: 10.5,
                    fontWeight: FontWeight.w600,
                    color: AppColors.textMuted,
                  ),
                  overflow: TextOverflow.ellipsis,
                ),
              ),
            ],
          ),
          const SizedBox(height: 8),
          Text(
            value,
            style: const TextStyle(
              fontSize: 15,
              fontWeight: FontWeight.w800,
              color: AppColors.textDark,
            ),
            overflow: TextOverflow.ellipsis,
          ),
        ],
      ),
    );
  }

  // Kerangka memuat dengan bentuk yang sama seperti kartu asli.
  Widget _buildLoadingSkeleton() {
    return ListView(
      physics: const BouncingScrollPhysics(),
      padding: const EdgeInsets.only(
        left: AppSpacing.md,
        right: AppSpacing.md,
        top: AppSpacing.sm + 4,
        bottom: 72,
      ),
      children: [
        const Row(
          children: [
            Expanded(child: ShimmerLoading(height: 72, radius: AppSpacing.radiusMd)),
            SizedBox(width: AppSpacing.sm),
            Expanded(child: ShimmerLoading(height: 72, radius: AppSpacing.radiusMd)),
            SizedBox(width: AppSpacing.sm),
            Expanded(child: ShimmerLoading(height: 72, radius: AppSpacing.radiusMd)),
          ],
        ),
        const SizedBox(height: AppSpacing.md),
        const Row(
          mainAxisAlignment: MainAxisAlignment.spaceBetween,
          children: [
            ShimmerLoading(height: 18, width: 140, radius: AppSpacing.radiusSm),
            ShimmerLoading(height: 22, width: 70, radius: AppSpacing.radiusPill),
          ],
        ),
        const SizedBox(height: AppSpacing.sm),
        const ShimmerLoading(height: 104, radius: AppSpacing.radiusMd),
        const SizedBox(height: AppSpacing.md),
        const ShimmerLoading(height: 104, radius: AppSpacing.radiusMd),
        const SizedBox(height: AppSpacing.md),
        const ShimmerLoading(height: 104, radius: AppSpacing.radiusMd),
      ],
    );
  }
}
