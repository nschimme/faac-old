#!/usr/bin/env python3
import gzip
import numpy as np
import pandas as pd
from sklearn.tree import DecisionTreeClassifier, export_text
from sklearn.metrics import accuracy_score, mean_absolute_error, confusion_matrix, classification_report

def compute_expanded_features_v3(df):
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

    base_feats = pd.DataFrame({
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
        'prev_ref_tEnv_last': prev_ref_tEnv_last,
        'clip': df['clip'],
        'channel': df['channel']
    })

    # 7. Compute 1-frame Future QMF Look-Ahead features per (clip, channel) sequence
    grouped = base_feats.groupby(['clip', 'channel'], sort=False)

    next_max_energy = grouped['max_energy'].shift(-1).fillna(base_feats['max_energy'])
    next_total_energy = grouped['total_energy'].shift(-1).fillna(base_feats['total_energy'])
    next_max_onset_ratio = grouped['max_onset_ratio'].shift(-1).fillna(1.0)
    next_h2_h1_ratio = grouped['h2_h1_ratio'].shift(-1).fillna(1.0)
    next_q1_frac = grouped['q1_frac'].shift(-1).fillna(0.25)
    next_centroid = grouped['centroid'].shift(-1).fillna(16.0)
    next_energy_ratio = next_total_energy / (base_feats['total_energy'] + eps)

    feats = base_feats.drop(columns=['clip', 'channel']).copy()

    feats['next_max_energy'] = next_max_energy
    feats['next_total_energy'] = next_total_energy
    feats['next_max_onset_ratio'] = next_max_onset_ratio
    feats['next_h2_h1_ratio'] = next_h2_h1_ratio
    feats['next_q1_frac'] = next_q1_frac
    feats['next_centroid'] = next_centroid
    feats['next_energy_ratio'] = next_energy_ratio

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

def run_recursive_simulation_v3(df, feats_df, dt_c, dt_ne, test_mask):
    """
    Sequential simulation feeding model's OWN previous frame predictions
    while utilizing 1-frame future QMF slot energy look-ahead.
    """
    test_df = df[test_mask].copy().reset_index(drop=True)
    test_feats = feats_df[test_mask].copy().reset_index(drop=True)

    pred_classes = []
    pred_nenvs = []
    pred_tenvs = []

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

            # Override prior-state columns with model's own predictions
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

def evaluate_lookahead_v3(df_path):
    df = pd.read_csv(df_path)
    feats_df, slots_all = compute_expanded_features_v3(df)

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

    print(f"\n==========================================")
    print(f"Evaluating V3 Look-Ahead Dataset: {df_path}")
    print(f"==========================================")

    for depth in range(3, 9):
        dt_c = DecisionTreeClassifier(max_depth=depth, random_state=42)
        dt_c.fit(X_train, y_train_c)

        dt_ne = DecisionTreeClassifier(max_depth=depth, random_state=42)
        dt_ne.fit(X_train, y_train_ne)

        # Ground-truth prior state + 1-frame future look-ahead (ceiling)
        ceil_pred_c = dt_c.predict(X_test)
        ceil_pred_ne = dt_ne.predict(X_test)
        ceil_class_acc = accuracy_score(y_test_c, ceil_pred_c)

        # Real recursive simulation + 1-frame future look-ahead
        rec_test_df, rec_pred_tenvs = run_recursive_simulation_v3(df, feats_df, dt_c, dt_ne, test_mask)
        rec_pred_c = rec_test_df['pred_frameClass'].values
        rec_pred_ne = rec_test_df['pred_numEnvelopes'].values

        rec_class_acc = accuracy_score(rec_test_df['ref_frameClass'], rec_pred_c)
        rec_nenv_acc = accuracy_score(rec_test_df['ref_numEnvelopes'], rec_pred_ne)

        # Strict full match on recursive simulation
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

        print(f"Depth {depth:2d} | Ceiling Acc: {ceil_class_acc*100:.2f}% | Recursive Acc: {rec_class_acc*100:.2f}% | Recursive NumEnv Acc: {rec_nenv_acc*100:.2f}% | Strict Full-Match Rate: {rec_full_match*100:.2f}%")

    # Detailed report for depth 6
    dt_c_6 = DecisionTreeClassifier(max_depth=6, random_state=42).fit(X_train, y_train_c)
    dt_ne_6 = DecisionTreeClassifier(max_depth=6, random_state=42).fit(X_train, y_train_ne)

    rec_test_df6, _ = run_recursive_simulation_v3(df, feats_df, dt_c_6, dt_ne_6, test_mask)

    print("\n=== Depth 6 Recursive Simulation Classification Report ===")
    print(classification_report(rec_test_df6['ref_frameClass'], rec_test_df6['pred_frameClass'], target_names=['FIXFIX (0)', 'FIXVAR (1)', 'VARFIX (2)', 'VARVAR (3)']))

    print("=== Depth 6 Recursive Confusion Matrix ===")
    cm = confusion_matrix(rec_test_df6['ref_frameClass'], rec_test_df6['pred_frameClass'])
    print(cm)

    print("\n=== Depth 6 Top Feature Importances (frameClass) ===")
    importances = sorted(zip(X_train.columns, dt_c_6.feature_importances_), key=lambda x: x[1], reverse=True)
    for name, imp in importances:
        if imp > 0.001:
            print(f"  {name:25s}: {imp*100:.2f}%")

def main():
    evaluate_lookahead_v3('probe/dataset/sbr_grid_dataset_v2_64k.csv.gz')
    evaluate_lookahead_v3('probe/dataset/sbr_grid_dataset_v2_96k.csv.gz')

if __name__ == '__main__':
    main()
