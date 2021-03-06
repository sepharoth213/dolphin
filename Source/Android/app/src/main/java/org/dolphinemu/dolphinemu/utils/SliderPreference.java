package org.dolphinemu.dolphinemu.utils;

import android.app.AlertDialog;
import android.content.Context;
import android.os.Bundle;
import android.preference.DialogPreference;
import android.util.AttributeSet;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import android.widget.Toast;

import org.dolphinemu.dolphinemu.R;

public class SliderPreference extends DialogPreference implements SeekBar.OnSeekBarChangeListener, View.OnClickListener
{
	private static final String androidns = "http://schemas.android.com/apk/res/android";

	// SeekBar
	private int m_max, m_value;
	private String m_key;
	private SeekBar m_seekbar;

	// TextView
	private TextView m_textview;

	public SliderPreference(Context context, AttributeSet attrs)
	{
		super(context, attrs);

		// Seekbar values
		m_value = attrs.getAttributeIntValue(androidns, "defaultValue", 0);
		m_max = attrs.getAttributeIntValue(androidns, "max", 100);
		m_key = attrs.getAttributeValue(androidns, "key");
	}

	@Override
	protected View onCreateDialogView()
	{
		LayoutInflater inflater = LayoutInflater.from(getContext());
		LinearLayout layout = (LinearLayout) inflater.inflate(R.layout.slider_layout, null, false);

		m_seekbar = (SeekBar) layout.findViewById(R.id.sliderSeekBar);
		m_textview = (TextView) layout.findViewById(R.id.sliderTextView);

		if (shouldPersist())
		{
			if (m_key != null && m_key.equals("Overclock"))
			{
				Toast.makeText(getContext(), getContext().getString(R.string.overclock_warning),
						Toast.LENGTH_LONG).show();

				float valueAsFloat = Float.valueOf(getPersistedString(Integer.toString(m_value)));
				float valueAsPercent = valueAsFloat * 100;

				m_value = Math.round(valueAsPercent);
			}
			else
			{
				m_value = Integer.valueOf(getPersistedString(Integer.toString(m_value)));
			}
		}

		m_seekbar.setMax(m_max);
		m_seekbar.setProgress(m_value);
		setProgressText(m_value);
		m_seekbar.setOnSeekBarChangeListener(this);

		return layout;
	}

	// SeekBar overrides
	@Override
	public void onProgressChanged(SeekBar seek, int value, boolean fromTouch)
	{
		m_value = value;
		setProgressText(value);
	}

	@Override
	public void onStartTrackingTouch(SeekBar seek)
	{
	}

	@Override
	public void onStopTrackingTouch(SeekBar seek)
	{
	}

	void setProgressText(int value)
	{
		m_textview.setText(String.valueOf(value));
	}

	@Override
	public void showDialog(Bundle state)
	{
		super.showDialog(state);

		Button positiveButton = ((AlertDialog) getDialog()).getButton(AlertDialog.BUTTON_POSITIVE);
		positiveButton.setOnClickListener(this);
	}

	@Override
	public void onClick(View v)
	{
		if (shouldPersist())
		{
			String valueToSave;
			if (m_key != null && m_key.equals("Overclock"))
			{
				float valueAsFloat = m_value / 100.0f;
				valueToSave = Float.toString(valueAsFloat);
			}
			else
			{
				valueToSave = Integer.toString(m_seekbar.getProgress());
			}

			persistString(valueToSave);
			callChangeListener(m_seekbar.getProgress());
		}
		((AlertDialog) getDialog()).dismiss();
	}
}
