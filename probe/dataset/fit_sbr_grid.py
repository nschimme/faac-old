#!/usr/bin/env python3
import gzip
import numpy as np
import pandas as pd
from sklearn.tree import DecisionTreeClassifier, export_text
from sklearn.metrics import accuracy_score, mean_absolute_error, confusion_matrix, classification_report

def compute_expanded_features_v2(df):
    slots = df[[f'slot{i}' for i in range(32)]].values.astype(np.float64)  # (N, 32)
    eps = 1e-9
    N = len(df)

    total_energy = np.sum(slots, axis=1)
    mean_energy = total_energy / 32.0 + eps
    max_energy = np.max(slots, axis=1)
    peak_slot = np.argmax(slots, axis=1)
    peak_to_mean = max_energy / mean_energy

    # 1. Per-quarter-frame energy ratios
    q1 = np.sum(slots[:, :8], axis=1) + eps
    q2 = np.sum(slots[:, 8:16], axis=1) + eps
    q3 = np.sum(slots[:, 16:24], axis=1) + eps
    q4 = np.sum(slots[:, 24:], axis=1) + eps

    q1_frac = q1 / (total_energy + eps)
    q2_frac = q2 / (total_energy + eps)
    q3_frac = q3 / (total_energy + eps)
    q4_frac = q4 / (total_energy + eps)

    # 2. Half-frame energy ratio (H2 / H1)
    h1 = q1 + q2
    h2 = q3 + q4
    h2_h1_ratio = h2 / h1

    # 3. Late-slot energy ratios
    late_8_frac = np.sum(slots[:, 24:], axis=1) / (total_energy + eps)
    late_4_frac = np.sum(slots[:, 28:], axis=1) / (total_energy + eps)

    # 4. Energy-weighted slot centroid
    slot_indices = np.arange(32, dtype=np.float64)
    centroid = np.sum(slots * slot_indices, axis=1) / (total_energy + eps)

    # 5. Local energy peaks count
    num_peaks_05 = np.zeros(N, dtype=int)
    for i in range(N):
        s = slots[i]
        p_thresh = 0.5 * max_energy[i]
        p_count = 0
        for k in range(32):
            left = s[k-1] if k > 0 else 0.0
            right = s[k+1] if k < 31 else 0.0
            if s[k] >= left and s[k] >= right and s[k] >= p_thresh:
                p_count += 1
        num_peaks_05[i] = p_count

    # 6. Maximum onset ratio
    ratios = np.zeros((N, 32))
    for k in range(1, 32):
        prev_mean = np.mean(slots[:, max(0, k-4):k], axis=1) + eps
        ratios[:, k] = slots[:, k] / prev_mean

    max_onset_ratio = np.max(ratios, axis=1)
    max_onset_slot = np.argmax(ratios, axis=1)

    # Cross-frame columns from v2 dataset
    prev_ref_frameClass = df['prev_ref_frameClass']
    prev_ref_numEnvelopes = df['prev_ref_numEnvelopes']
    prev_ref_tEnv0 = df['prev_ref_tEnv0']
    prev_ref_tEnv_last = df['prev_ref_tEnv_last']

    feats = pd.DataFrame({
        'total_energy': total_energy,
        'log_total_energy': np.log10(total_energy + 1.0),
        'mean_energy': mean_energy,
        'max_energy': max_energy,
        'peak_slot': peak_slot,
        'peak_to_mean': peak_to_mean,
        'max_onset_ratio': max_onset_ratio,
        'max_onset_slot': max_onset_slot,
        'h2_h1_ratio': h2_h1_ratio,
        'q1_frac': q1_frac,
        'q2_frac': q2_frac,
        'q3_frac': q3_frac,
        'q4_frac': q4_frac,
        'late_8_frac': late_8_frac,
        'late_4_frac': late_4_frac,
        'centroid': centroid,
        'num_peaks_05': num_peaks_05,
        'prev_ref_frameClass': prev_ref_frameClass,
        'prev_ref_numEnvelopes': prev_ref_numEnvelopes,
        'prev_ref_tEnv0': prev_ref_tEnv0,
        'prev_ref_tEnv_last': prev_ref_tEnv_last
    })
    return feats, slots

def derive_tenv_and_fres(p_c, p_ne, o_slot, prev_last):
    if p_c == 0: # FIXFIX
        p_tenv = [0, 8, 16] if p_ne == 2 else [0, 16]
        p_fres = [1, 1] if p_ne == 2 else [1]
    elif p_c == 1: # FIXVAR
        b1 = int(o_slot)
        b2 = min(15, b1 + 4)
        b3 = min(16, b2 + 4)
        if p_ne == 4:
            p_tenv = [0, b1, b2, b3, 18]
            p_fres = [1, 0, 0, 1]
        elif p_ne == 3:
            p_tenv = [0, b1, b2, 16]
            p_fres = [1, 0, 1]
        else:
            p_tenv = [0, b1, 16]
            p_fres = [1, 1]
    elif p_c == 2: # VARFIX
        t0 = max(0, min(16, int(prev_last - 16))) if prev_last > 16 else max(0, min(3, int(o_slot)))
        p_tenv = [t0, 7, 16]
        p_fres = [1, 1]
    else: # VARVAR
        b0 = max(0, min(16, int(prev_last - 16))) if prev_last > 16 else max(0, min(2, int(o_slot) - 2))
        b1 = int(o_slot)
        b2 = min(12, b1 + 4)
        b3 = min(15, b2 + 4)
        p_tenv = [b0, b1, b2, b3, 18]
        p_fres = [1, 0, 0, 1]
    return p_tenv, p_fres

def run_recursive_simulation(df, feats_df, dt_c, dt_ne, test_mask):
    """
    Simulate stateful encoder execution per (clip, channel) sequence.
    Frame 0 uses sentinel -1 for prev state.
    Subsequent frames use the model's own predictions from the preceding frame.
    """
    test_df = df[test_mask].copy().reset_index(drop=True)
    test_feats = feats_df[test_mask].copy().reset_index(drop=True)

    pred_classes = []
    pred_nenvs = []
    pred_tenvs = []

    # Group by clip and channel
    groups = test_df.groupby(['clip', 'channel'], sort=False)

    for (clip_id, ch), group in groups:
        indices = group.index.tolist()

        # Sentinels for frame 0
        prev_p_c = -1
        prev_p_ne = -1
        prev_p_t0 = -1
        prev_p_tlast = -1

        for idx in indices:
            row_feat = test_feats.iloc[[idx]].copy()

            # Override cross-frame columns with model's own previous predictions
            row_feat['prev_ref_frameClass'] = prev_p_c
            row_feat['prev_ref_numEnvelopes'] = prev_p_ne
            row_feat['prev_ref_tEnv0'] = prev_p_t0
            row_feat['prev_ref_tEnv_last'] = prev_p_tlast

            p_c = dt_c.predict(row_feat)[0]
            p_ne = dt_ne.predict(row_feat)[0]

            o_slot = row_feat['max_onset_slot'].values[0]
            p_tenv, p_fres = derive_tenv_and_fres(p_c, p_ne, o_slot, prev_p_tlast)

            pred_classes.append(p_c)
            pred_nenvs.append(p_ne)
            pred_tenvs.append(p_tenv)

            # Update state for next frame
            prev_p_c = p_c
            prev_p_ne = p_ne
            prev_p_t0 = p_tenv[0]
            prev_p_tlast = p_tenv[-1]

    test_df['pred_frameClass'] = pred_classes
    test_df['pred_numEnvelopes'] = pred_nenvs

    return test_df, pred_tenvs

def evaluate_recursive_and_ceiling(df_path):
    df = pd.read_csv(df_path)
    feats_df, slots_all = compute_expanded_features_v2(df)

    unique_clips = sorted(df['clip'].unique())
    np.random.seed(42)
    train_clips = set(np.random.choice(unique_clips, size=int(len(unique_clips)*0.8), replace=False))
    test_clips = set(unique_clips) - train_clips

    train_mask = df['clip'].isin(train_clips)
    test_mask = df['clip'].isin(test_clips)

    X_train = feats_df[train_mask]
    X_test = feats_df[test_mask]

    y_train_c = df.loc[train_mask, 'ref_frameClass']
    y_test_c = df.loc[test_mask, 'ref_frameClass']

    y_train_ne = df.loc[train_mask, 'ref_numEnvelopes']
    y_test_ne = df.loc[test_mask, 'ref_numEnvelopes']

    dt_c = DecisionTreeClassifier(max_depth=6, random_state=42)
    dt_c.fit(X_train, y_train_c)

    dt_ne = DecisionTreeClassifier(max_depth=6, random_state=42)
    dt_ne.fit(X_train, y_train_ne)

    # 1. Ground-Truth Ceiling Evaluation (v2 Ceiling)
    ceil_pred_c = dt_c.predict(X_test)
    ceil_pred_ne = dt_ne.predict(X_test)
    ceil_class_acc = accuracy_score(y_test_c, ceil_pred_c)
    ceil_nenv_acc = accuracy_score(y_test_ne, ceil_pred_ne)

    sub_test_df = df[test_mask].reset_index(drop=True)
    sub_test_feats = feats_df[test_mask].reset_index(drop=True)

    ceil_matches = 0
    for i in range(len(sub_test_df)):
        ref_c = sub_test_df.at[i, 'ref_frameClass']
        ref_ne = sub_test_df.at[i, 'ref_numEnvelopes']
        if ceil_pred_c[i] == ref_c and ceil_pred_ne[i] == ref_ne:
            o_slot = sub_test_feats.at[i, 'max_onset_slot']
            prev_last = sub_test_feats.at[i, 'prev_ref_tEnv_last']
            p_tenv, _ = derive_tenv_and_fres(ceil_pred_c[i], ceil_pred_ne[i], o_slot, prev_last)
            ref_tenv = [sub_test_df.at[i, f'ref_tEnv{j}'] for j in range(ref_ne + 1)]
            borders_ok = True
            for j in range(min(len(p_tenv), len(ref_tenv))):
                if ref_tenv[j] != -1:
                    if abs(p_tenv[j] - ref_tenv[j]) > 2:
                        borders_ok = False
                        break
            if borders_ok:
                ceil_matches += 1
    ceil_full_match = ceil_matches / len(sub_test_df)

    # 2. Recursive/Self-Consistent Simulation Evaluation
    rec_test_df, rec_pred_tenvs = run_recursive_simulation(df, feats_df, dt_c, dt_ne, test_mask)
    rec_pred_c = rec_test_df['pred_frameClass'].values
    rec_pred_ne = rec_test_df['pred_numEnvelopes'].values

    rec_class_acc = accuracy_score(rec_test_df['ref_frameClass'], rec_pred_c)
    rec_nenv_acc = accuracy_score(rec_test_df['ref_numEnvelopes'], rec_pred_ne)

    rec_matches = 0
    for i in range(len(rec_test_df)):
        ref_c = rec_test_df.at[i, 'ref_frameClass']
        ref_ne = rec_test_df.at[i, 'ref_numEnvelopes']
        if rec_pred_c[i] == ref_c and rec_pred_ne[i] == ref_ne:
            p_tenv = rec_pred_tenvs[i]
            ref_tenv = [rec_test_df.at[i, f'ref_tEnv{j}'] for j in range(ref_ne + 1)]
            borders_ok = True
            for j in range(min(len(p_tenv), len(ref_tenv))):
                if ref_tenv[j] != -1:
                    if abs(p_tenv[j] - ref_tenv[j]) > 2:
                        borders_ok = False
                        break
            if borders_ok:
                rec_matches += 1
    rec_full_match = rec_matches / len(rec_test_df)

    print(f"\n==========================================")
    print(f"Dataset Evaluation: {df_path}")
    print(f"==========================================")
    print(f"Ground-Truth Ceiling | frameClass Acc: {ceil_class_acc*100:.2f}% | numEnvelopes Acc: {ceil_nenv_acc*100:.2f}% | Strict Full-Match: {ceil_full_match*100:.2f}%")
    print(f"Recursive Simulation | frameClass Acc: {rec_class_acc*100:.2f}% | numEnvelopes Acc: {rec_nenv_acc*100:.2f}% | Strict Full-Match: {rec_full_match*100:.2f}%")

    print("\n=== Recursive Simulation Classification Report (frameClass) ===")
    print(classification_report(rec_test_df['ref_frameClass'], rec_pred_c, target_names=['FIXFIX (0)', 'FIXVAR (1)', 'VARFIX (2)', 'VARVAR (3)']))

    print("=== Recursive Simulation Confusion Matrix ===")
    cm_rec = confusion_matrix(rec_test_df['ref_frameClass'], rec_pred_c)
    print(cm_rec)

    # Drift analysis
    print("\n=== Latching / Drift Analysis ===")
    rec_test_df['prev_pred_c'] = rec_test_df.groupby(['clip', 'channel'])['pred_frameClass'].shift(1).fillna(-1)

    # Consecutive non-FIXFIX prediction streaks
    non_fix_mask = rec_test_df['pred_frameClass'] != 0
    ref_fix_mask = rec_test_df['ref_frameClass'] == 0
    false_pos_streak = non_fix_mask & ref_fix_mask
    print(f"False non-FIXFIX predictions on FIXFIX ground truth: {false_pos_streak.sum()} / {ref_fix_mask.sum()} ({false_pos_streak.mean()*100:.1f}%)")

def main():
    evaluate_recursive_and_ceiling('probe/dataset/sbr_grid_dataset_v2_64k.csv.gz')
    evaluate_recursive_and_ceiling('probe/dataset/sbr_grid_dataset_v2_96k.csv.gz')

if __name__ == '__main__':
    main()
