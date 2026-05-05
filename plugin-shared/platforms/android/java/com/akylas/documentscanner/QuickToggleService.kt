package com.akylas.documentscanner

import android.app.PendingIntent
import android.content.Intent
import android.os.Build
import android.service.quicksettings.TileService
import android.util.Log

class QuickToggleService : TileService() {

    override fun onClick() {
        val context = applicationContext

        // Get the launch intent for your app
        val pm = context.packageManager
        val intent = pm.getLaunchIntentForPackage(context.packageName)
        // Log.d("QuickToggleService", "quicktile onClick: ${context.packageName}, intent: $intent")

        if (intent == null) {
            return
        }

        // Set flags to bring existing activity to front if running
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_REORDER_TO_FRONT or Intent.FLAG_ACTIVITY_CLEAR_TOP)

        if (Build.VERSION.SDK_INT < 34) {
            startActivityAndCollapse(intent)
        } else {
            startActivityAndCollapse(PendingIntent.getActivity(context, 0, intent, PendingIntent.FLAG_IMMUTABLE))
        }
    }

    override fun onStartListening() {
        super.onStartListening()
        // Optional: update tile state or icon dynamically
        // val tile = qsTile
        // tile?.label = "My App"
        // tile?.updateTile()
    }

    override fun onStopListening() {
        super.onStopListening()
    }
}